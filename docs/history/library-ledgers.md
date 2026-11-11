# Batched library ledgers

The ledger tools retain measured counts and reasons. This history records
the evidence behind replacing one library-wide measurement with another;
the current mechanisms and measurements belong in REIMPLEMENTATION.md.

## The run of 92026499

The 84-movie library covered 2,289,640 frames. Its drift-only calibration
left these recorded outcomes, with the Ozzie's Fort native row added after
its chest messages joined the asset catalogue:

| Mode | Saved game / save RAM matches | Pictures: match / drift / differ | Places: match / drift / differ |
|---|---|---|---|
| Strict | 42 / 80 | 1,062 / 178 / 6,508 | 436 / 163 / 271 |
| Native | 36 / 72 | 583 / 97 / 7,068 | 93 / 461 / 330 |

## The run of bfd75c49

The next batched run checked all 84 movies and all 56 routes. Its failed
checks were timing, fallbacks, uncompared calls, strict progress, native
places and recorded outcomes. The routes all passed. The ledger separates
timing and comparison-boundary changes from failures needing code changes:

- **R-Series and the shipping fallback.** The battle timing merge
  `09132520` exposed an existing task-lifetime error. Closing the menu clears
  its task records, then the closing task starts a fade wait. Paying its
  outstanding time can cross a frame before that wait records its state.
  Cleanup discarded the task in that interval. Strict play stopped at
  frame 37,306 at `$C2:83FB`; shipping play ran 28 original instructions
  occupying 41 bytes. Reversing that merge's timing alone lets the unfixed
  runtime finish. The runtime now retains the task until the wait has begun;
  `suspend_test` forces a frame across this interval. Fresh R-Series play
  finishes all 65,437 frames without the fallback.
- **Effect graphics comparisons.** `03ba3f61` retired the unpacker into its
  callers. All 102 calls of `battle_load_effect_graphics` now cross an
  interrupt, so a clean shadow report proves none of their complete loads.
  The fuzz runner can notify both counters the loader waits on. Two seeds
  compare 2,048 complete randomized loads, all six effects and both upload
  chunk sizes, with zero mismatches or skips. The gate repeats this evidence.
- **Repeated-hit timing.** `45483053`, merged in `09132520`, fitted the
  effect's mostly interrupted calls at 54554 instead of 3000. The batched
  run's two complete Spekkio calls then have errors up to 88,609 clocks.
  A fresh trial fit lowers that to 65,492, still over the 50,468 limit:
  the nested hit's ordinary-call estimate overcharges this short path.
  Restore the earlier 3000 and retain the nested hit's 45460. The hit's
  remaining aggregate drift is explicit, and its callers are refitted with
  those retained rates. Separate short and ordinary paths before fitting
  the shared rates again. The fresh complete Spekkio calls both match and
  their worst error falls to 44,891 clocks, inside the 50,468 limit.
  Refitting the member-tech dispatcher's own share then exposes the same
  problem across its mixed effects: 31178 creates a 63,323-clock outlier.
  It retains 17772 until those paths can be measured separately; fresh
  complete Spekkio calls match with worst error 44,930 clocks. The movie's
  complete shadow run has no byte mismatch and passes the subset timing
  check, which judges worst cases; means remain library-wide measurements.
- **Race timing.** The interrupted measurement added in `09132520` exposes
  1,700 stretches of `scene_race_scale_after_crono`. Its calibrated row and
  copy parts alone cost more than the original's measured time, while its
  own rate is already at the minimum 1. The projected +3.64% after fitting
  the rows is recorded as model debt; a caller's scalar cannot correct it.
- **Experience timing.** The library's mean fit assigns 132501 to the
  experience-sharing dispatcher's small own share. Fresh Ozzie's Fort play
  puts an ordinary call 59,116 clocks off, beyond the worst-case limit;
  Flea and Slash also exceed it. Retaining 21758 while fitting its parts
  reduces the fresh Ozzie's Fort error to 44,941. The mean drift remains
  explicit until ordinary and interrupted paths can be measured separately.
- **Native places.** Replacing only the timing model with its form before
  `a21cfa3c` restores the previous protected count in each of the seven
  failures: Flea 19, Slash 18, Nizbel II 6, Ozzie's Fort 11, Crono's Trial 6,
  Death Peak wind 6 and Fiona's Forest 8. The same experiment restores
  Cyrus 16 and Sun Stone 18, whose aggregate counts improved but individual
  protected visits moved. These are recorded-input drift, not missing C
  paths. Their new observations retain per-kind pins and explicit reasons.
  The same counterfactual restores the lost Magus-joins saved-game match
  and the late Lavos and late Omen Zeal save-RAM matches.
- **Fair shuffle.** Text opens four frames early and the song branches at
  frame 6,266, one frame before the reference; one event-memory byte differs.
  The overworld teardown, `627fcfa8`, moves it: a build of its first parent
  ends identically with all seven places on time, while the merge's build
  reproduces the batched drift and six places on time.
  Running the hand ports with the original's time reproduces the reference
  through the end, over 6,608,036 port calls. Reversing only the battle timing
  or map-scroll rates does not remove this drift.
- **Other strict end-state drift.** Original-timed diagnostics also replay
  Dalton, Magus at North Cape, the tech sweep and late Omen Zeal through the
  reference's saved-game states to the end: respectively 20,147,465,
  16,992,233, 54,081,391 and 20,118,673 hand-port calls. These observations
  establish that the changed recorded-input positions account for their
  changed game progress. Separate original-timed end captures match both
  the saved game and save RAM for all four. Those diagnostic runs report
  sound-queue overruns, so they establish end-state equality only; they are
  not gate passes and supply no ledger observations.
  The new rate fit also moves Denadoro, Crono's Trial and Terramutant from
  exact strict endings to drift. Fresh original-timed end captures recover
  identical saved games and save RAM for these three too.
  The same diagnostic recovers Beyond Time and Death Peak's save-RAM
  matches lost after calibration, with identical saved-game ends as well.
- **Reference fallback attribution.** The reference's recorded outcomes
  are unchanged. Retiring the unpacker's comparisons in `03ba3f61` changes
  which still-open caller owns original instructions: `$C0:56A6` holds 664
  in Black Tyrano; `$C1:B967` holds 40,308 rather than 36,037 across the tech
  sweep, late Lavos and late Omen Zeal. Replaying the last 600 frames from
  reference-machine snapshots with that historical build reproduces all
  four counts and identical reference end states. Their shipping plays use no original
  fallback. The baseline keeps these open extents visible, with their
  per-movie counts, rather than treating them as new missing game code.

The original batched evidence supplies the rate fit and the movies not
replayed on the branch. Fresh targeted and sampled runs replace their
movies' fallback and outcome artifacts; diagnostic counterfactuals never
supply ledger observations. The scratch evidence manifest records every
replacement. No trace, state dump or extracted asset is committed.

The fresh set is the 37 movies in `tools/gate_movies.txt`, plus R-Series,
fair shuffle, Flea, Slash, Nizbel II, Ozzie's Fort, Crono's Trial, Death Peak
wind, Fiona's Forest, Rust Tyrano, the Spekkio tech sweep and late Omen Zeal.
The remaining 35 movies and the long routes retain the batched observations.
Every refreshed outcome exception identifies a fresh branch replay; the
other changed exceptions identify the batched `bfd75c49` replay. Reference
outcome hashes remain unchanged.
The Ocean Palace dark-ending exception is removed: both the batch and the
fresh native replay end visibly. Its recorded-input drift remains open;
the fresh replay misses the reference's Mammon Machine room visit.

## Regressions exposed after the teardown sample

The whole-library run at `3c91834d` exposed failures absent from the old
37-movie sample. The repair branch starts at `6cf000b3`. Historical binaries
were built in the repair worktree and run one movie at a time with the same
input and cartridge save. Full outcome artifacts distinguish saved game,
save RAM, picture samples and location sequence; no historical binary or
diagnostic counterfactual supplies new baseline observations.

**Failures in executable behavior.** Bucket Lavos's first strict refusal is
frame 48239 at `$C1:0C2D`: an empty ally target is already the marked target.
The original compares them before indexing a target and returns on equality;
the guard incorrectly rejected that no-op. Field retirement (`299b2905`)
exposed the refusal in strict play. Before battle retirement (`9b1597a3`),
shipping play already refused three battle entries there but kept intact
interrupt stubs. After retirement removed battle scratch state, the original
fallback reaches `$C3:0633`'s unpacking block move at frame 48496 and
overwrites `$0500-$0507`. These are damaged vectors, not another valid stub
image. Accepting the already marked empty target prevents the fallback;
strict reaches all 196000 frames and shipping has zero fallback and refusals.

Spekkio's nothing-to-steal message uses selector 13. Both the original
`$CD:01C3` and the C accessor read `$D1:EE99 + 13 = $D1:EEA6`, an operand
of the adjacent instruction. The twelve-byte nominal table was too short
for the actual resource view; its explicit overlay now spans fourteen bytes.
The failure predates the cartridge-read checking merge `865819f7`: native
time accounting (`253d674c`) changes the recorded input's path and exposes
it in a pre-battle-retirement binary too. The complete 134646-frame native
replay now ends visibly, and native-shape no longer rejects the read.

**The two video findings were attribution changes.** `9b1597a3` transferred
map-to-menu work to `overworld_menu_fade_finished`, updating the exceptions
for line differences but omitting their visible-write counterparts. Parent
and current shipping logs have identical sites, counts and scanlines:
Fiona's Forest has HDMA disable `$C2:253D` at line 95 and display blank
`$C2:8025` at 172; Sun Stone has the same writes at 131 and 208. Renaming
the two existing exceptions, with this evidence on each line, clears both
findings without accepting a new write.

**Strict timing regressions.** Field scratch retirement `dd2ba214`, merged
in `299b2905`, removed some counted accesses without replacement estimates.
The retained counts cover layer streaming, location loading, event/object
preparation, script restarts, dialog shading, party stepping and contact
selection. They preserve the machine's calibrated work, with no new charge
to the native host, which never performed comparison scaffolding. Flea's
first object-position difference is frame 3427: the missing layer-counter
write lets another command fit in frame 3426. Spekkio's return to the field
loses 2103 estimated master clocks at frame 63294 and first changes an
object delay at 63295. Full strict replays restore the historical artifacts
for Black Tyrano, Flea, Slash, Heckran, Magus at North Cape, Rust Tyrano,
unequipped armor, Spekkio and Bucket Lavos.

Battle held continuations `e4ea6b60`, merged in `9b1597a3`, removed two
trial return-frame writes from setup dispatch and reassigned the setup
return's cost. R-Series first changes visibly at frame 8543. Dalton's
startup trace at 5623 isolates the missing return-word estimate; its first
affected recorded picture is 5698. Keeping dispatch work and return work
at their respective retained rates restores both complete strict artifacts.
Flea, Slash, R-Series, Rust Tyrano and Spekkio pass their existing strict
outcome ratchets after these repairs.

**Changes that remain ledger work.** `54099ed8` calibrated the library's
rates but retained older observations for 35 movies (the previous section
records that boundary). Dragon Tank and Son of Sun pass before that commit
and fail immediately after it, in both strict and native modes. Their strict
artifacts do not change across the later teardown merges. The same strict
calibration-only attribution holds for Giga Gaia, Golem Twins, Mud Imp and
Zombor; the restored teardown witnesses above can still differ from pins
that predate calibration. The first changed recorded picture in these
strict comparisons is frame 298, during startup; it is a sampled observation,
not a claim that the first changed pixel occurs exactly then.

Every failed native outcome was replayed on both sides of `253d674c`:
Black Tyrano, Flea, Slash, Dalton, Dragon Tank, Giga Gaia, Golem Twins,
Heckran, Magus at North Cape, MotherBrain, Mud Imp, Nizbel II, Ozzie's Fort,
R-Series, Rust Tyrano, Son of Sun, Spekkio and Bucket Lavos. Each changes at
that native time-model merge (the first changed recorded picture is 298).
The earlier calibration also changes all of those except Flea, Slash and
Spekkio. Replaying both sides of field retirement does not change their
native outcome artifacts. The model charges transfers, retired work and
frame budgets; reverting that accounting to fit a fixed input movie would
undo its intended correction.

The three native place ratchets likewise move at `253d674c`: Cyrus 18 to
16 timely arrivals, Fiona's Forest 7 to 4, and Sun Stone 20 to 12. The first
location transition moves from frame 48 to 49 as DMA time is charged
(`d8f2c34f` within that merge). Cyrus retains the same 18-place order:
its arrival at location 497 moves from 3735 to 3737 against reference 3706,
crossing the 30-frame tolerance. Fiona's location 502 moves 4400 to 4402
against 4371, then misses the reference's location 424 visit. Sun Stone's
later fixed inputs diverge after its era transitions: the earlier model
continues through 21 locations, the new one through 13. These are shown
time-model changes; their counts and per-kind pins await the ledger commit.

MotherBrain has one additional correct change: `20ad985f` rereads the live
action record after targeting can reuse its low-pointer storage. Reversing
only those reads restores its pre-battle-retirement artifact, including the
five picture samples at 43798-43802, while saved game, save RAM and places
already agree. Do not restore the stale cached tech selection to recover
the pictures; the next ledger needs this reason.

The sample now has 58 movies and 1418355 frames, including every failed
witness except Nizbel II and MotherBrain (148758 and 135481 frames).
Shorter witnesses for those two remain follow-ups. Bucket Lavos and
Spekkio remain despite their cost because no shorter input reproduces
their actual contract and resource faults. No outcome or place baseline
was changed by these repairs.

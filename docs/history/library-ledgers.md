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

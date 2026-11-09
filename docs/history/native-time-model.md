# Native time model measurements

## The first frame-level model

**Measured.** The places a run enters, by frame, against the reference play's
(`tools/places.py`; `ct_dev --locations` and `ct_native --locations`, the
suite's shadow runs keeping the reference's): how many of the reference's
places a run enters within 30 frames of the reference's frame, among those it
enters in the reference's order. Over the 53 library movies (460 places):

| | `ct_native`, places on time | strict mode, movies ending as the reference's |
|---|---|---|
| Before | 240 | 34 |
| The sound driver's time | 294 | 37 (339 places on time) |
| And loads (`ct_native` only) | 299 | |

Timing the game's own work was measured over the gate's 37-movie sample
(`tools/native_check.py --movies ...`, against the suite's shadow runs): 194 of
the reference's 293 places entered on time before, 209 after; the places
entered within 5 frames of the reference's, 148 before and 196 after; and the
attract demo, whose places are the game's own time, 1,229 frames behind the
reference at its end before and 211 after. Movies ending as the
reference's: 18 of 36 compared before, 18 of 37 after (`events-fair-rescue`,
`late-omen-terramutant` and `menus-shop-inn` among those that now do,
`menus-end-of-time` and `battle-magus` among those that now drift). The new
time took `late-omen-battles` into a battle where an enemy waits for its
target to move with none in sight, which the program's C path refused
though the movement's entry takes it (`battle_enemy_movement_needs_target`
now says the waiting movement goes on with none); it now reaches its end.
The native outcomes were re-recorded with it (`tools/outcomes_exceptions.txt`,
each line's reason naming the change); the protections lost leave
`tools/outcomes_baseline.txt` (the end state of `menus-end-of-time`, the save
RAM of `events-death-peak`, a place of `events-ocean-palace-lavos` and
checkpoint pictures in eight movies, each a run whose places keep to the
reference's within the window but whose frames moved), and the matches
gained join it; `tools/places_baseline.txt` takes the counts the change
brings (none lower).

Strict mode's three, `battle-magus`, `battle-tyrano-lair-walls` and
`field-factory-switch`, now end as the reference's, and none ends worse; in
`ct_native` the title shows 3 frames early where it showed 26 early, and
`gato-battle` enters 10 of its 11 places on time where it entered 9.
`tools/native_check.py --check` holds `ct_native` to it, a ratchet on
`tools/places_baseline.txt`, and `tools/strict_check.py` reports it for strict
mode.

## Keeping retired operations in the native account

The sample on `aa7e5769`, and again after merging the scene teardown
(`a7d8a04d`), starts at 17 of 37 movies ending as the reference does and
209 of 293 places entered on time. Restoring `charge_legacy_accesses`,
`charge_legacy_io_accesses` and `charge_legacy_rom_accesses` in the native
backend raises reference endings to 19, with places unchanged at 209.
The machine backend and the calibrated rates are unchanged.

Fair rescue now matches the saved game, the save-point movie its SRAM, and
Beyond Time its SRAM (its saved game still differs). No saved-state or place
protection is lost. The re-recording moves six protected picture checkpoints:
attract-demo 19200; battle-guardian 5700 and 6000; battle-menu-buttons 3000;
events-ocean-palace-lavos 7800 and 8100. Reasons accompany the changed
exceptions, and newly matching checkpoints are protected with outcomes.py.
Native outcome observations change from 362 match / 213 drift / 1432 differ
to 366 / 215 / 1429 over the same sample. This is a timing improvement, not
a claim that the remaining battle and ending differences are harmless.

The optional per-tag time report leaves all 37 sampled outcome artifacts
unchanged when enabled. A synthetic native timing test checks nesting,
unknown tags, uncounted work, the restored access prices, and fractional
clocks carried across the scheduler taking its account.

## Charging a handler to its frame

Separating service charges made in the vblank and line handlers from the
main thread's debt preserves 19 of 37 reference endings and 209 of 293
places on time. No protected outcome is lost. The won Jetbike race's picture
digest changes, with the same counts of matching, nearby and differing
checkpoints; its exception is deliberately re-recorded. The synthetic
timing test reproduces the old extra lag frame after a two-frame load and
verifies that handler work no longer causes it. Both handlers still reduce
the main thread's frame budget.

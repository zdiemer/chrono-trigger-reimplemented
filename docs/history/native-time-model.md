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

## Timing the transfers that bypass the bus

Supported `vid_snes_copy` operations used to move their bytes through the
observer or host callbacks without charging DMA time. They now use the
same eight clocks per byte as ordinary main-thread video transfers. This
includes the power-on clear of 81,536 bytes, or 652,288 clocks. The title
appears at frame 49 instead of 48 (the reference is 51).

The sample goes from 19 to 20 of 37 reference endings and from 209 to 210
of 293 places on time. The Terramutant movie now matches the saved game;
Ocean Palace enters one more reference place on time. Beyond Time still
misses the ending and now changes 4,189 SRAM bytes: it returns to the load
screen at frame 13,039, where the preceding model did so at 29,845. Its
saved-game difference is 54 bytes instead of 829. This is a changed replay
outcome, not completion of the ending; its SRAM protection is explicitly
re-recorded with this reason. The remaining timing work is in TODO.

The transfer regression covers the supported RAM clear, RAM copy and VRAM
upload, including zero as a 64 KiB length. Three such transfers take four
lag frames; the old backend took none. The sample's outcome observations
are 368 match / 216 drift / 1,422 differ after the re-recording.

## Letting the field defer work at its deadlines

The native budget used to answer true unconditionally. It now takes the
pending work account and follows the original predicate's window: NMI at
225, reopening at 240, then the requested limit after frame wrap. The tests
check the one-clock boundaries, repeated queries without double charging,
handler costs, renewal after a wait and a load that spans a whole frame.

The sample goes from 20 to 21 reference endings; places remain 210 of 293.
The End of Time menu now matches both saved game and SRAM. The save-point
movie still matches the saved game but changes 389 SRAM bytes, an explicitly
reviewed loss of its SRAM protection while load timing remains incomplete.
The attract demo's worst early arrival grows from 308 to 394 frames; all
50 places remain in order. The new outcome observations are 377 match /
214 drift / 1,415 differ. These mixed effects remain visible in the ledger.

## Pricing the unpacker's actual work

The native backend now counts retired operations' actual accesses before
applying their retained rate, alongside the replacement-access charges.
The unpacker's stream reads, copied output and RAM-port operations therefore
contribute directly. Both backends read one retained-rate table; the machine
values remain 4,060 and 4,057 thousandths. Nested excluded spans and host
inspection reads do not enter that account.

Two synthetic streams with identical command counts but copied runs of
three and eighteen bytes used to cost the same 2,496 clocks. They now cost
2,159 and 3,134, with the correct output bytes in both cases. Direct entry,
entry overhead, nested exclusions and the no-cartridge timing fixture are
also checked. The native RAM-port services charge their ordinary accesses
outside the unpacker at the caller's tag, as the machine backend does.

With the transfer and frame-budget corrections already in place, the
sample goes from 21 to 22 reference endings and from 210 to 211 places on
time. The End of Time gate now matches, and the save-point SRAM match is
restored. Tyrano Lair Walls loses its saved-game match: it enters the
walkway at frame 3,696, against 3,697 before and 3,703 in the reference, and
ends with 44 different saved-game bytes, including inventory, character
progression and battle cursors. Its SRAM still matches and all five places
remain on time. That loss is explicitly re-recorded, with the battle-input
follow-up deferred until the battle and menu teardowns.

Beyond Time still differs, returning to the load screen at frame 12,304;
its 4,189-byte SRAM difference remains exposed. The attract demo's worst
early arrival falls from 394 to 344 frames. The outcome observations are
386 match / 243 drift / 1,377 differ. The 47 native movies outside the sample
still need the batched library run; none of these measurements is a
whole-library recalibration.

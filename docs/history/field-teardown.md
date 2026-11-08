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

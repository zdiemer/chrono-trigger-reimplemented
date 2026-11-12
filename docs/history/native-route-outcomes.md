# Native route outcomes

Results for the 56 routes selected by `tools/suite.py --long-routes`, using
`ct_native --server` and shipping `ct_drive --native on --generated on`.
Frame numbers are each run's own outcome frame. A pass means the route's
outcome checks succeeded. The existing 21 hand-run exclusions are listed below.
The audit ended by request on 2026-11-11. Stopped and unstarted runs are
explicit exceptions, not passes; rerun them after native saves can use
complete frame-end bases during a route instead of replaying from power-on.

All runs use the native implementation in `5aa8489c`. Successful early runs
are retained from before the search correction described in [MOVIES.md](../MOVIES.md).
Later runs use the corrected search. The false telepod prefix result was
discarded; its shared approach and the complete Lavos-loss route were rerun.
The Beyond Time ending timeout also precedes that correction; its map-exit
search is recorded as an observed cost limit, not a claim that later helpers
cannot finish it.
Phase continuations reconstruct their full input histories and verify the
resulting RAM digest before continuing.

Observed outcomes: native 42 passes, 2 failures, 3 timeouts, 3 stopped, 6 not run; shipping 49 passes, 7 not run.
The native long-route gate selects the 42 passing full routes and both fast
prefixes, and reports all 14 exceptions with their reasons.

| Fast prefix | Native | Shipping |
|---|---|---|
| `telepod_lavos:shell` | pass (5162) | pass (5102) |
| `beyond_time:form2` | pass (3770) | pass (3754) |

| Route | Native | Shipping | Notes |
|---|---|---|---|
| `battle_menu_buttons` | pass (5070) | pass (5070) |  |
| `beyond_time` | timeout | pass (107048) | Six-hour ending-continuation timeout in Bell plaza ($051); last reported frame 81750. Final battles won. |
| `black_tyrano` | pass (46840) | pass (49740) | Native continued from the verified scene checkpoint. |
| `bucket_lavos` | stopped | pass (174308) | Reached the inner cavern at 97298; the prepared continuation was not run. |
| `castle_flea` | pass (41972) | pass (42987) |  |
| `castle_slash` | pass (38713) | pass (39923) |  |
| `cyrus` | pass (39634) | pass (39826) |  |
| `dactyl_65m_bc` | pass (2810) | pass (2810) |  |
| `dactyl_flight_65m_bc` | pass (3992) | pass (3992) |  |
| `dalton` | pass (35309) | pass (39567) |  |
| `death_peak` | pass (22892) | pass (23890) |  |
| `death_peak_return` | pass (22613) | pass (22964) |  |
| `death_peak_wind` | pass (4696) | pass (4676) |  |
| `denadoro_fight` | pass (9145) | pass (8553) |  |
| `dragon_tank` | pass (16008) | pass (17597) |  |
| `epoch_lavos` | FAIL | pass (7762) | Waits over Lavos at frame 9587, scene $02; unserved music-marker read. See divergences below. |
| `factory_switch` | pass (6875) | pass (6869) |  |
| `fair_clone` | pass (8394) | pass (8333) |  |
| `fair_rescue` | pass (6635) | pass (7297) |  |
| `fair_shuffle` | pass (6820) | pass (6723) |  |
| `fair_strength` | pass (4365) | pass (4276) |  |
| `field_12kbc_1f4` | pass (2953) | pass (2953) |  |
| `field_12kbc_1f5` | pass (2993) | pass (2993) |  |
| `field_12kbc_1f6` | pass (2873) | pass (2873) |  |
| `field_65m_bc` | pass (2943) | pass (2921) |  |
| `fionas_forest` | pass (36217) | pass (36213) |  |
| `floor_switches` | pass (3501) | pass (3501) |  |
| `giga_gaia` | pass (34811) | pass (33615) |  |
| `golem_twins` | pass (19371) | pass (21361) |  |
| `guardian` | pass (23434) | pass (24947) |  |
| `last_village_12kbc` | pass (9269) | pass (9269) |  |
| `lavos_lost` | pass (15637) | pass (16332) | Rerun after correcting goal tests inside already visited search cells. |
| `luccas_mother` | pass (15661) | pass (15711) | Native continued from the verified return checkpoint with the corrected search. |
| `magic_cave` | pass (6146) | pass (6126) |  |
| `magus_joins` | pass (13738) | pass (13633) |  |
| `menu_sweep` | pass (6424) | pass (6424) |  |
| `motherbrain` | timeout | pass (155522) | Stopped after six CPU hours in the escort-pod charge search ($100); last completed frame 44933. |
| `mud_imp` | pass (32170) | pass (32572) |  |
| `new_game_plus` | pass (3096) | pass (3096) |  |
| `nizbel_ii` | timeout | pass (132380) | Stopped after six CPU hours searching the lower hall ($12A) toward the door switch; last completed probe 66706. |
| `north_cape` | pass (29993) | pass (34118) |  |
| `northern_ruins_switch` | pass (3871) | pass (3811) |  |
| `ocean_palace_lavos` | pass (30586) | pass (30539) |  |
| `ozzies_fort` | pass (38568) | pass (39249) |  |
| `proto_dome` | pass (24900) | pass (24286) |  |
| `r_series` | stopped | pass (63684) | Stopped by request after the shutter escape (33288); last completed probe 33504, lower lab $0E6. |
| `rust_tyrano` | pass (22306) | pass (23329) |  |
| `save_point` | pass (3270) | pass (3270) |  |
| `son_of_sun` | FAIL | pass (18206) | Fails the landing check at frame 5794, still flying. Fixed coast settles at X=536 versus shipping 544. |
| `sun_stone_finish` | stopped | not run | Stopped by request after recovering the Moon Stone (12881); last completed probe 13099, mayor's house $032. |
| `tech_sweep_spekkio` | not run | not run | Deferred because power-on slot replay makes searches too slow; rerun after bases can be taken during play. |
| `telepod_lavos` | not run | not run | Deferred because power-on slot replay makes searches too slow; rerun after bases can be taken during play. |
| `trial` | not run | not run | Deferred because power-on slot replay makes searches too slow; rerun after bases can be taken during play. |
| `unequipped_armor` | not run | not run | Deferred because power-on slot replay makes searches too slow; rerun after bases can be taken during play. |
| `zeal_palace` | not run | not run | Deferred because power-on slot replay makes searches too slow; rerun after bases can be taken during play. |
| `zombor` | not run | not run | Deferred because power-on slot replay makes searches too slow; rerun after bases can be taken during play. |

## First observed differences

- **Epoch into Lavos:** with identical input, at frame 3179 shipping has
  entered location $1B0 while native still reports world map $1F0. Native
  reaches the question 40 frames later. The later failure has a concrete
  dependency: event $114 reads sound status through the generic memory-read
  command at offsets $0C6A, $0CB3 and $0CE7, waiting for markers 1, 2 and 3.
  The native RAM backing returns zero for $00:2142, so the first wait cannot
  finish. Native stops at frame 9587 with scene mode $02 instead of $26.
  Shipping reaches Lavos's cavern and its first window at frame 7762.
  Fixed by the cartridge-address audit, which answers the event's sound-status
  read from the C sound driver: native passes through the shell at frame 7802.
- **Beyond Time:** the first common-input difference observed is frame
  2566: `peek 7E1802 2` returns `400c` in native and `300c` in shipping,
  Crono's X at 196 versus 195 pixels. This timing difference does not
  establish the cause of the later timeout. The native continuation won
  both final battles and entered Bell plaza at frame 81312, but rebuilding
  stacks for each search branch exhausted its six-hour limit. Its last
  completed protocol step was frame 81750; the timeout interrupted a load.
  No replay mismatch preceded it. Shipping completes at frame 107048.
- **MotherBrain:** the first common-input difference is at frame 3401:
  native still reports $1F1 (600 A.D.) while shipping has entered $1F2
  (2300 A.D.). Native later clears the lower corridor battle and starts
  the escort-pod charge search on Geno Dome's main floor ($100). That
  attempt was stopped after more than six CPU hours; the configured
  21600-second process wait also reported timeout after host suspends.
  Its last completed step was frame 44933, followed by an unfinished
  `load 1805`; the escort-pod flag ($7F:013E mask $02) was still clear.
  No replay mismatch preceded the stop. This bounds the search cost,
  not the reachability of the pod. Shipping completes at frame 155522.
- **Nizbel II:** the first common-input difference is frame 2709:
  `peek 7E1892 2` (field object 9's Y) returns `ff30` in native and
  `ee32` in shipping. Native later clears the maze and the lower hall's
  ambushes, winning the last observed fight at frame 65434. It was stopped
  after more than six CPU hours searching toward the door switch in $12A;
  the configured process wait reported timeout after host suspends. The
  last completed probe was frame 66706, followed by `load 120`. No replay
  mismatch or gameplay assertion preceded the stop. The early position
  difference does not identify its cause. Shipping completes at 132380.
- **Son of Sun:** at the first common-input difference, frame 3756,
  native reports world map $1F0 while shipping has entered $1F2. Both
  subsequently reach the future. The flight helper releases RIGHT at
  X=534 and waits twelve frames; native then reports X=536 while shipping
  reports X=544. The helper assumes that coast reaches the landing tile.
  Native never leaves flight mode 2 and fails the landing check at frame
  5794. Shipping wins the fight and regains field control at frame 18206.
  This is a landing-approach failure, not evidence about the boss fight.
- **R Series (stopped):** the first common-input difference is frame
  2658: world X (`peek 7E0283 2`) is `a003` in native and `9f03` in
  shipping, 928 versus 927 pixels. Native restored power and escaped the
  shutters, then was stopped by request during the lower-lab search at
  a pending `load 351`; the last completed probe was frame 33504. No
  gameplay assertion or replay mismatch preceded this stop.
- **Bucket to Lavos (stopped):** the first common-input difference is
  frame 2566: `peek 7E1802 2` returns `4019` in native and `3019` in
  shipping (Crono's X, 404 versus 403 pixels). Native reached the inner
  cavern checkpoint at frame 97298. Its prepared continuation was not run.
- **Sun Stone (stopped):** native recovered the Moon Stone at frame 12881
  and was stopped by request during the exit search, with `load 156` pending
  after probe 13099. Shipping was not run in this audit, so no paired first
  difference is claimed. The six unstarted routes likewise have no comparison.

The telepod search failure was a route-helper bug: a valid goal position
inside an already visited coarse cell was discarded. Goal tests now precede
that discard, and the shell checkpoint requires location $1D3. Native enters
the shell at frame 5162 and reaches the loss ending at 15637; shipping uses
5102 and 16332. [TODO.md](../TODO.md) holds the open game and restore-cost debts.

## Validation

The sampled gate (`tools/suite.py --ct-dev build/main/ct_dev.exe -j 2
--evidence`) ended with `suite OK`, including both native fast prefixes at
the frames above. The 62 touched Python tests passed, as did
`route_server_test`, `audio_replay_test`, `fiber_test` and `native_time_test`.
The Release build used `CT_WERROR=ON` and two build jobs. No long-route audit
was restarted after the stop request.

## Existing hand-run exclusions

These routes are outside the existing long-route gate and were not audited
here. They remain callable with `route_check --mode native ROUTE`.

| Route | Existing reason |
|---|---|
| `blackbird_save` | Cartridge-save preparation through long prerequisites. |
| `cronos_trial` | Long play from the nearest stock save; outside the gate's movie selection. |
| `dino_age` | Long alternate-ending route, run by hand. |
| `heckran` | Existing shipping route loses with the stock save's supplies; see TODO.md. |
| `heckran_prepare` | Cartridge-save preparation through long prerequisites. |
| `legendary_hero` | Long alternate-ending route, run by hand. |
| `memory_lane` | Long alternate-ending route, run by hand. |
| `nizbel` | Long play from the nearest stock save; outside the gate's movie selection. |
| `people_of_the_times` | Long alternate-ending route, run by hand. |
| `prepare_luccas_mother` | Cartridge-save preparation through long prerequisites. |
| `rainbow_shell` | Long play from the nearest stock save; outside the gate's movie selection. |
| `rainbow_shell_trial` | Long play from the nearest stock save; outside the gate's movie selection. |
| `reunion` | Long alternate-ending route, run by hand. |
| `successor_of_guardia` | Long alternate-ending route, run by hand. |
| `sun_stone` | Long play from the nearest stock save; outside the gate's movie selection. |
| `sun_stone_save` | Cartridge-save preparation through long prerequisites. |
| `tech_sweep` | Long tech-coverage play, run by hand. |
| `the_oath` | Long alternate-ending route, run by hand. |
| `unknown_past` | Long alternate-ending route, run by hand. |
| `what_the_prophet_seeks` | Long alternate-ending route, run by hand. |
| `yakra` | Long play from the nearest stock save; outside the gate's movie selection. |

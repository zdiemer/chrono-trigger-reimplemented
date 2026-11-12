# Native route outcomes

Results for the 56 routes selected by `tools/suite.py --long-routes`, using
`ct_native --server` and shipping `ct_drive --native on --generated on`.
Frame numbers are each run's own outcome frame. A pass means the route's
outcome checks succeeded. The existing 21 hand-run exclusions are listed below.
The audit ended by request on 2026-11-11. Stopped and unstarted runs are
explicit exceptions, not passes; rerun them after native saves can use
complete frame-end bases during a route instead of replaying from power-on.

The original audit uses the native implementation in `5aa8489c`. Successful early runs
are retained from before the search correction described in [MOVIES.md](../MOVIES.md).
Later runs use the corrected search. The false telepod prefix result was
discarded; its shared approach and the complete Lavos-loss route were rerun.
The Beyond Time ending timeout also precedes that correction; its map-exit
search is recorded as an observed cost limit, not a claim that later helpers
cannot finish it.
Phase continuations reconstruct their full input histories and verify the
resulting RAM digest before continuing.

Observed outcomes: native 42 passes, 2 failures, 3 timeouts, 3 stopped, 6 not run; shipping 49 passes, 7 not run.
Subsequent fixes pass Epoch into Lavos and Son of Sun (below). The native
long-route gate now selects 44 passing full routes and both fast prefixes,
and reports the 12 remaining exceptions with their reasons.

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
| `epoch_lavos` | pass (7802) | pass (7762) | Retested after serving the event's sound-status read; see below. |
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
| `son_of_sun` | pass (16041) | pass (18226) | Retested with the settled-position flight approach; both win without retries. |
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
  A subsequent per-frame replay of `battle-son-of-sun` resolves the earlier
  sparse observation: the first differing location is the load-screen
  transition at frame 1668 (native `$000`, shipping still `$1B1` until 1669).
  After loading, boarding first differs at 3045 (native travel 4, shipping
  still 0); the future location changes at 3783 versus 3753. These input and
  load phases precede the landing symptom. From each route's future checkpoint,
  releasing RIGHT at X=534 leaves the same tile goal, 536. One frame later
  shipping has queued goal 544 while native has stopped at 536. After twelve
  released frames both remain still. `overworld_vehicle_flies_a_tile` finishes
  a tile, and `overworld_vehicle_flies` starts another from the sampled pad;
  the different input phase accounts for the extra tile, without a different
  speed or coasting rule. The helper now approaches the target, then taps and
  checks the settled coordinate before landing. Both full routes pass:
  native lands at 4984 and finishes at 16041 (41 seconds); shipping lands at
  4956 and finishes at 18226 (94 seconds). Regression tests cover immediate
  and delayed pad sampling, reverse approaches, and blocked movement. The
  Son of Sun exception is removed; no recording was changed.
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

## Ocean Palace playback investigation

Per-frame replay of the unchanged recordings in native and shipping first
differs at the load-screen transition (1668 versus 1669). After loading,
the first party movement differs at 3215 in `battle-golem-twins` (native
X=217, shipping 216) and 3305 in `events-ocean-palace-lavos` (native
Y=110.9375, shipping 111.9375). The first finale dialogue opens at 4355
versus 4356; recorded A presses subsequently see different dialogue states,
first at 4929. The native Mammon room return is 16966, shipping 16967;
the older claim that this visit is missing no longer describes this build.

The video escapes are title-map fills at `$7E:5AEB` and `$7E:59DD`, before the palace.
The Twins recording loses its battle and returns to the title at 15493,
accounting for its second pair of escapes. The new `vid_fill_vram_half`
service preserves the pixel half and continues the tile-map cursor in both
backends; it replaces that fill and the shared unused-plane fill. The next
BG1 fill retains the source that both callers have just supplied, the blank
tile, and now uses `vid_send_vram_fill` instead of a channel-state escape.

The finale's black ending is a timed fade, not a missing display-enable
call. Without changing input, native goes dark at 29819 and leaves the
scene for `$10F` at 30767; shipping leaves at 30724. Both are beyond the
recording's 30364-frame end and its last two B presses (30088 and 30181).
Continuing dialogue from there reaches the Last Village, whose party
exchange needs B. The focused native continuation accepts it and regains
visible field control at 37213. No fade duration, recording, or exception
was changed.

The shipping routes pass the finale at 30549 and the Twins with slot 3
saved at 21361. A fresh native Twins route wins at 15247 and regains field
control at 18257; its subsequent exit search was stopped after minutes of
power-on replays. The native finale rerun was likewise stopped during its
early Mammon-room exit search. These are bounded continuations, not new
full-route passes; the original audit's full native passes remain above.

## Fixed-input timing losses

The follow-up to `253d674c`, based on `a1fcd274` with the landing helper and
video fills corrected, replays the unchanged five recordings through
both servers, observing location, world and party positions, dialogue,
control, travel mode, story and battle-menu state after **every frame**.
The first observed location difference is the common load-screen transition
at 1668 (native) versus 1669 (shipping). Uninitialized scratch bytes are not
gameplay evidence. After loading, the first movement differences are:

| Recording | Frame | Native | Shipping |
|---|---:|---|---|
| `events-cyrus` | 3031 | world X 345 | 344 |
| `events-fionas-forest` | 3667 | world Y 439 | 440 |
| `events-sun-stone` | 2634 | leader Y 368.9375 | 367.9375 |
| `battle-dalton` | 2815 | leader Y 174.9375 | 175.9375 |
| `late-omen-zeal` | 3201 | leader X 57 | 56 |

These precede the lost outcomes. The native backend runs handlers once per
frame and accounts main-thread work at waits and budget queries; the
shipping backend can interrupt that work. Their input phase need not be
identical. `native_time_test` and `timing_test` still pass the budget,
handler-charge and wait boundaries. The continuations below test whether
the game can perform the missed actions; they do not establish equal
frame counts or calibrate away a recorded input's failure.

**Cyrus and Fiona.** Cyrus enters the past at 3737 and 12692, versus 3706
and 12661, both 31 frames later. The targeted `cyrus:repaired` route passes
at 16404 and 16375, after both era trips, obtaining and delivering the
tools, and paying for the first repairs. Its earlier `asked` and `tools`
checkpoints are 6364/6365 and 11466/11467. This isolates the two late
arrivals without repeating the full grave route. Fiona's recording reaches
the present at 4402 versus 4371, then misses the village entrance. The
`fionas_forest:village` prefix enters location 424 and proves control in
both builds, at 5402 and 5343. Run these with `route_check --mode native
--mode on:on cyrus:repaired fionas_forest:village`. Neither is a blocked
frame-budget loop. The current Cyrus recording ends showing in the ruins'
entry room `$041`, without video escapes; the old blank-ending report no
longer describes it. This prefix does not claim that its later recorded
battle inputs complete the quest.

**Sun Stone.** Both recordings reach the present-day Keep. At frame 9986,
when the recorded downward walk starts, native's leader is at Y=199.9375
and shipping's at 203.9375. At 10282 the recording turns upward: native has
only reached 495.9375, shipping 498.9375. Shipping has triggered the exit
and enters the world at 10296; native turns away and only exits at 10635,
after later downward presses. The subsequent boarding and flight inputs
therefore start from the wrong position. The recorded native path has 13
location transitions, shipping 21; this is a missed door, not evidence of
a missing map or quest operation.

A focused continuation keeps each recording's inputs through that exit,
then uses the corrected flight helper, `sun_stone_finish`'s boarding and
entrance helpers, and position-driven walks around the mayor's counter and
to the Keep's stone. The walks target observed room coordinates and wait
for dialogue and control, without writing game state. It recovers the Moon
Stone at 13403/13040, replaces it at 16235/15948, enters the charged Keep
at 18839/18465, and receives Wondershot and Sunshades with world-map control
at 22278/21951. The checks require quest states `$17`, `$37` and `$F7`,
the Moon Stone entering and leaving the bag, and both rewards in it.
This completes the portion the recording misses. The full
`sun_stone_finish` route's search-cost exception remains: this continuation
does not rerun its earlier general-purpose searches.

**Dalton and Queen Zeal.** Both fixed-input recordings lose battles before
their final progress differs. At the Dalton Plus and Queen Zeal setups,
their battle-menu state differs at 25527 and 5324 respectively. Later
identical A inputs reach different active members: Dalton at 25938 selects
member 2 in native and 1 in shipping; Zeal at 6554 selects 0 and 1. Dalton
returns to the title at 36560 versus 31686; the later fixed inputs reload the Blackbird in native and the Last
Village in shipping. The full state-driven `dalton` route passes at 35288
and 39541, including the Blackbird's destruction, the winged Epoch and
world-map control. Queen Zeal's recording returns to the title at 24145
versus 17469, before Mammon Machine. A focused continuation uses the same
recording through frame 5000, waits for `battle_menu_open`, then calls
`Run.fight("party", ...)` until living party members have defeated the
enemies. It wins at 17232 and 15194 with zero retries. Advancing dialogue
with an eight-frame press interval opens the next battle at `$1A6` at
18486 and 16470. That proves the first recorded loss is avoidable in both
builds; it is not a claim about the remainder of the Black Omen ending.
No movie, timing estimate or outcome exception was refreshed.

## Video-clear fingerprint check

The follow-up sampled gate passes shadow comparison on all 58 movies,
trace identity, timing, video-write placement, both shipping and native
route prefixes, fallbacks, refusals, all 58 strict and native runs, the
scaffolding-free build, sound, liveness, source checks and evidence. Its
only failure is three native picture fingerprints: `attract-demo`,
`battle-golem-twins` and `battle-golem-twins-gameover`.

Isolated binaries identify `$7E:59DD`'s BG1 clear as the entire cause.
Disabling only that clear reproduces all three prior fingerprints; undoing
the half-plane service or the fixed-source-byte correction does not. The
first changed sampled neighbours are 11998, 16798 and 12598 respectively,
all on a return to the title; 20, 15 and 13 picture neighbours change.
Game state, SRAM, the complete place observations and every protected
picture remain unchanged. At frame 12600 of the game-over recording, the
old title visibly contains stale field tiles; the clear removes them.
The shipping native renderer and PPU decoder agree on all 12970 frames
of that recording, as on all 30366 frames of the Ocean Palace finale.

The 625 Python unit tests pass, as do `video_test`, `native_time_test` and
`timing_test`; `portstyle` and `portsmell` have no new findings. The build
uses `CT_WERROR=ON`. The three old picture pins are still unchanged;
the gate's last line is `FAILED: outcomes` until their deliberate update
is authorized. This is separate from the fixed-input losses above, whose
movies, timing estimates and exceptions remain unchanged.

## Original audit validation

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

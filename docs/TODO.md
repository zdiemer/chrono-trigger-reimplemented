# Open follow-ups

Loose ends the milestone docs record in passing, collected so they don't get
lost. Each line says where the detail lives. Milestones themselves are in
[APPROACH.md](APPROACH.md), [GENERATED.md](GENERATED.md),
[REIMPLEMENTATION.md](REIMPLEMENTATION.md), [MATCHING.md](MATCHING.md)
and [PLATFORM.md](PLATFORM.md); which of these to work on next, and why the
rest waits, is [PLAN.md](PLAN.md).

## Correctness and accuracy

- **Out-of-contract script states.** The event loops at `$C0:58ED`, `$C0:5967`, `$C0:5990` and
  `$C0:5AA3` already run as C for valid scripts; their hand-backs preserve
  out-of-buffer script positions, changed data banks and object state the
  typed map cannot represent. Supporting those states needs a storage or
  script contract, not a second copy of the same loop. The screen runner's
  `$CD:165F` hand-back is for bytes `$FA`-`$FE`, past its command table;
  establish semantics for the original's out-of-table dispatch before
  admitting them. No shipped script is known to use these paths.
- **Recorded outcomes across the library.** All 84 movies are recorded and
  have strict and native ratchets (REIMPLEMENTATION.md, "Recorded
  outcomes"); their differences are excepted as debts. Resolve the excepted
  saved-game, picture and place differences before a teardown relies on
  those observables; none of the 84 is free of differences in either mode. Native
  power-on save RAM also starts at zero rather than the reference's `0x60`.
  A storage teardown must supply the same saved-game
  projection from the new storage; a picture hash alone cannot establish
  that a visual difference cannot affect play.
- **Shorter regression movies.** Reduce the expanded gate sample only with
  recordings that reproduce its failures: Bucket Lavos's empty marked ally
  target and Spekkio's "nothing to steal" sound selector. Add shorter outcome
  witnesses for Nizbel II and MotherBrain, which remain library-only because
  of their length (MOVIES.md, "Adding movies").
- **Keep the unreached ledger's preconditions when retiring the machine.**
  Preserve the assertions and resource assumptions behind
  `tools/unreached_safe.toml` and the retired subsystems' inventories
  (REIMPLEMENTATION.md's teardown sections; history/field-teardown.md and
  history/scene-unreached.tsv). In particular:
  - Replace event diagnostics and graphics-error handoffs with native
    failures; preserve the word-OR, text-table and graphics-table bounds.
  - Keep the reset chord and vblank-only restart contracts.
  - Preserve core's palette bounds, the warp radius ramp, overworld record
    and boarding-selector checks, and `walk_a_leg`'s stationary invariant.
  - Keep `field_location_is_a_number` and `location_record_is_one` as
    assertions: a non-record offset can unpack into the stack or scratch.
  - Preserve battle sprite/output bounds, animation resource banks and
    indexes, startup OAM lookup, action/script table bounds, and text
    language/stack separation when their storage or assets move.
  - Recheck excluded tile-set blocks, BG2 left drift and unused scene-tile
    loads if assets become editable; recheck the eight-sprite zone proof
    if screen height changes. Arithmetic-dead paths need no substitute.
  Ledger byte totals change with retirements; the exact runs and their
  evidence belong in those inventories, not a second count here.

- **Exercise both sign-refresh arms after returning from a screen.**
  `set_the_sign_again`'s increments at `$C2:2603` and `$C2:267E` were
  reviewed-equivalent, not executed by fuzzing before retirement. Record a
  return from map view/time dial with a visible sign, including refresh 255,
  to protect the modulo-256 update through recorded outcomes.

- **A battle's start, one fuzz state in about 4,000.** `field_battle_begins`
  (`tools/fuzz_routines.toml`, both variants; seeds 1-4) mismatches once or
  twice per 5,000 states: a field work cell (`$7E:01C3`, `$01DB`), the
  palettes (`$2122`-`$2359`) and an enemy's battle palette
  (`battle_enemy_placement`) differ. The old constraints show it (seed 4), so it
  predates the full map's search; look at an enemy without a sprite taking a
  palette slot (`field/palettes.c`, `object_take_enemy_palette_slot`) mid-search.
- **The battle grid's reads past the combatants.** With the party off the grid,
  `battle_mark_combatants_on_grid` (`battle/battlefield.c`) reads slots 11-13,
  whose objects 23-25 put their Y in the drawing scratch (`$7E:1D2E`-`$1D30`).
  `draw_frame` (`battle/draw_objects.c`) now writes the frame's X (`$2E`-`$2F`)
  plainly, as the early-boss movies showed the grid reading it; the frame's Y
  (`$30`, object 25's) is still written only as scaffolding, so the native
  shape reads it stale wherever slot 13 is present (`$7E:9702` nonzero). No
  movie has shown that read yet: write `$30` plainly too, or show it unread.
- **The second movie batch's timing exceptions.** After one drift-only calibration
  over the 84-movie library, these entries are still over a limit and listed in
  `tools/timing_exceptions.txt` (the second batch's block): `,` (after), `battle_effect_anchor_jump` (vblank-tail), `field_draw_the_objects_in_view` (vblank-worst), `field_refresh_effects` (vblank-tail), `menu_choose_equipment_member` (drift), `menu_choose_on_main_menu` (drift), `menu_run_the_main_task` (drift), `event_cmd_take_formation` (vblank-tail), `field_enter` (worst), `menu_fade_palette_row` (vblank-tail), `menu_load_record_sprites` (drift), `menu_mode_enter` (drift), `menu_window_draw_frame_only` (vblank-tail), `menu_window_frame` (vblank-tail), `scene_program_hands_back` (drift).
  Each is work whose time follows its data or its path (a pose's graphics, a
  window's size, a palette row, a load with the screen blanked) or a jump the port
  takes with no bus access; the vblank check finds none of their writes moved.
  Make each follow its data (`game/charge.h`) or time the part as its own routine,
  and drop its line.
- **Heckran's route in the shipping mode.** `heckran` wins the fight in the
  reference play (`battle-heckran`), but in the shipping mode (`route_check
  heckran`, `on:on`) the party runs out of Mid Tonics and Ethers and its
  fight search gives up after 101 tries with Heckran at about 770 of 2100 HP.
  The route is in `HAND_RUN_ROUTES` until its policy wins there: spend MP and
  items more sparingly, or back the search up further than the last decisions.

### Timing and other accuracy work

- **Hand ports' time is an estimate**, so `--native on --generated on` drifts from
  the original (battles play out differently). That difference is accepted
  (REIMPLEMENTATION.md, "Can a hand port be exact?"); the timing gate bounds the errors
  (REIMPLEMENTATION.md, "The timing gate"). Entries whose time follows a path
  inside one routine, or a loop of the port's own, remain on
  `tools/timing_exceptions.txt` (the menu's text, glyphs and
  wave table, the scene's circle, the battle's wave
  windows and HP gradient, the free-slot walk that starts a menu object, and
  the own loops of the effect slots, the object
  drawing and its underlays); a dispatcher's handlers are attributed to
  instead (REIMPLEMENTATION.md, "Dispatchers"), and the menu text's control codes would
  be too once their handlers are entries. Adding a movie or a port means
  recalibrating, since the drift limit holds only for the measured value.
  `--generated only` stays exact.
- **Strict mode's battles a few frames off** (REIMPLEMENTATION.md, "Calls that
  outlast a frame, and the battles' drift"). Of the 45 `drifts` movies, 29
  still part with a battle menu's shift in force, a median 7 frames. What was
  found and not fixed:
  - *The passes' knife edge.* A battle's main-loop pass takes two or three
    frames by whether its work crosses a frame boundary, and the turn timers
    count passes, so a few lines of error a frame flips a pass. In
    `battle-denadoro`'s second battle (frames 6700-7020) `battle_draw_objects`
    estimated +7.9% (1.27 M clocks over 320 frames) where the library's drift
    is -0.12%: the per-scene spread of its parts.
  - *Handlers calibrated on their entry wrapper.* `battle_draw_behind`'s
    calls with no effect playing take the original 84 clocks and are
    estimated at 659 (+684% in that window): the machine wrapper's reads and
    writes of the drawing page's cells are most of a short call's bus clocks,
    and the calibration takes them in, but `battle_draw_objects` runs the same
    work inline as a part without them. Leaving a wrapper's accesses out of
    its entry's estimate (`charge_uncounted_*`) would make the two agree.
  - *A field wait no lag frame explains.* In `battle-black-tyrano` the
    strict run is already 8 frames early when the battle starts: the field's
    frames match the reference's one for one, but at frames 7012-7019 an
    object's frame staging is rewritten 7 frames apart in the reference and 1
    in strict mode. Something the event waits on there is not counted in
    frames; find it before the battle start can be judged.
  - *The menus' long calls.* `menu_open_load_screen`, `menu_draw_load_screen`
    and `menu_run_the_main_task` are interrupted on every call and their
    handler goes on in another task, so even the interrupted measurement
    leaves them out; they keep a hand-set `cost_milli` of 5000.
  - *The battle songs' load* (the time model's item "The handlers' share of
    an upload", below): 9-12 frames early at every battle with a song of its own.
  - *Dispatchers' extreme values from the interrupted calibration* (checked,
    not a wait): `battle_member_attacks_from_bank_fd` (2506 to 519998) and
    `battle_main_loop_start` (3000 to 114458) carry a sliver of own clocks
    beside their parts (368 bus clocks over 23 stretches; 264,032 of 20.5 M),
    and the rest of the original's time once the parts' estimates are taken out
    lands on it: about 8,300 clocks a stretch for the first, no frame's worth,
    so no spin is in it. The ten floored to 1 (`battle_set_up`,
    `battle_enemy_flags_turn`, `menu_open_main_menu`, ...) are the same with
    the parts a little over: `battle_set_up`'s own time (1,229,158 clocks in
    `battle-black-tyrano`) equals its parts' measured own times
    (`battle_setup_state` 773,972 and `battle_setup_graphics` 377,006, with
    the window and bounds at their rates), so the handlers' time isn't taken
    out twice. A dispatcher's own value is ill-conditioned this way; it moves
    only its own few clocks.
  - *The ending picture dispatcher's calibration.* Fitting
    `scene_ending_start_mode7_picture` (10452 to 166947) puts a vblank call
    81,358 clocks off: its own clocks are a sliver beside its parts, and its
    calls' paths differ. It keeps its value; separate the paths before
    changing that rate.
  - *The experience-sharing dispatcher's calibration.* Its ordinary and
    interrupted paths need separate estimates. The batched fit over 120
    stretches assigns 132501 to its small own share; a fresh Ozzie's Fort
    replay then puts an ordinary call 59,116 clocks off, beyond the 50,468
    limit (Flea and Slash also exceed it). It retains 21758 with a drift
    exception while its parts use their fitted rates.
  - *Short and ordinary hits need separate estimates.* The repeated-hit
    interrupted fit in `45483053` raised its rate from 3000 to 54554, pushing
    two complete Spekkio calls up to 88,609 clocks off. Retain 3000 and the
    nested hit's 45460; fitting the latter's ordinary calls at 48677 still
    overcharges the repeated effect's short path. Split the estimates before
    removing the hit's drift exception.
    The member-tech dispatcher's mixed effects have the same problem:
    refitting its own share at 31178 after retaining those rates creates
    a 63,323-clock complete-call outlier. Keep 17772 until its effect kinds
    can be measured separately.
  - *The race's row estimates exceed their caller's time.* Across 1700
    interrupted stretches of `scene_race_scale_after_crono`, the calibrated
    row and copy parts alone exceed the measured original time. Its own
    rate is already 1; the remaining +3.64% needs a model of the differing
    row work, not a negative rate for the caller.
- **Charge the endings' swept window by its lines.**
  `scene_ending_sweep_window` (`game/scene/endings.c`) retains one calibrated
  rate in `game/time_tags.c`, although its work follows the lines and corners
  drawn. Its old worst-case exception records 64,755 clocks of error.
  Measure that work in the native model before changing the retained rate;
  its independent shadow comparison has retired.

- **Fuzzing coverage of existing ports.** Audit the still-registered ports
  against `tools/fuzz_routines.toml` and `docs/ports.tsv` for missing receipts.
  Retired subsystems need coherent-state tests and recorded outcomes instead
  of new comparisons against deleted registry rows.

- **Dino Age's line-interrupt discrepancy in a native fixture.** Before
  battle retirement, the hand-run `dino_age` route with Core seed 1
  (technoplaza ct-state23 slot 2, 193389 frames; MOVIES.md) reached The End
  in reference and shipping play but exposed a `battle_irq` discrepancy:
  at the wait, the horizontal scroll byte was `FF` in the original and
  `00` in the port, persisting for the next 15 passes. It also reproduced
  without generated code over the Core interval 133140-177869; the ending
  interval was clean. Core seed 2, the route's default, finished at 176261
  with a clean comparison and identical final PNG. Both inputs completed
  in strict and native modes. The cause remains unestablished: recreate
  the seed-1 state in a native line-effect fixture and check the intended
  scroll transition. Its registry comparison no longer exists.
- **IRQ timing simplifications** (RUNTIME.md, "Interrupt timing"): hardware's
  ~3.5-dot trigger delay isn't modeled, and an IRQ pending across `CLI` is
  taken one instruction early.
- **S-DSP** is sample-granular, and note starts differ from Snes9x by one KON
  poll (RUNTIME.md, "S-DSP"). The audio comparison table predates the DMA and
  refresh timing fix and should be re-measured.
- **Big-endian hosts** are untested (CMake warns).
- **Packaging on Linux and macOS** isn't checked on a machine that never built
  it, and `dist/` there relies on the player's machine having SDL2 (PLAYING.md,
  "Build"). A macOS bundle would copy the dylib and rewrite its install name.
- **ZSNES state import** approximates DSP voice history, timer phase and
  in-frame position (RUNTIME.md, "ZSNES save states"), so those runs are for
  coverage only.
- **`ct_fuzz` doesn't use generated blocks**, only hand ports (GENERATED.md).
- **`$213F`'s bit 7 is the beam latch's flag in the runtime's PPU**
  (`runtime/snes/ppu.c`), where the hardware has the field's parity (and the
  latch's flag in bit 6). The scenes read it as the frame's parity
  (`tm_odd_frame`: the endings' slow credits, `$7E:9AB3`), so on the runtime
  they move on the frames after the random mixer latched the beam, not every
  other frame. Shadow mode compares the runtime with itself; the oracle's
  traces of the Beyond Time ending would show it.

- **The attract sequence's credit pages mismatch in fuzzing.** The broad
  scene-start fixture had 11 VRAM mismatches in 384 comparisons at seed 1,
  unchanged by the scene review (history/scene-teardown-preconditions.md,
  "Comparison gaps before retirement"). Earlier startup fixtures also
  differed in OAM and the Mode 7 product. The narrowed credit-page recipe
  passes, but does not establish why the broad states differ. Isolate the
  setup mode and VRAM-read state with a native fixture; its registry
  comparison has retired. Keep this separate from the clean bounded recipes.

- **What the hand ports alone left** (REIMPLEMENTATION.md, "Phase 4a, the
  shipping mode's fallbacks, chunk 11"):
  - Entries no movie's shadow run compares, checked by fuzzing alone: the
    planet's count down to the reset (`$7E:63F5`) and its fade out
    (`$7E:6403`) and the world map scene's note of the Epoch's arrival
    (`$C6:EE31`). Strict mode's own timing reached them; a movie that plays
    them would compare them.
  - Entries that run nested inside another port's stretch and are compared
    only through it (tools/uncompared_baseline.txt): the field frame's
    continuations (`$C0:00A4`-`$C0:00B7`) and the planet's setup (`$7E:6248`,
    one call, inside the scene's first frame).
  - The scene scripts' interpreter differs from the original in about one
    random state in 17,500, in a channel's word register (`scene_run_sequence`
    at seeds 2 and 3, its continuation after a wait, `$C3:0C28`, at seed 1;
    tools/fuzz_routines.toml): a command reached only from a random script
    place. Find the command.
  - An object's script started over (`$C0:597D`) reads the script past what
    the map declares on random states (`accessor out of range` in fuzzing,
    `event_script`): its guard doesn't bound where the object's first
    function starts.
- **The tech sweep's default fight policy.** Seed 1 reaches the Core on the
  hand ports but repeatedly wipes near frame 94000 with the Core at 9409 HP.
  Improve its spending and targeting or choose a policy seed that reaches
  the outcome without original-code handbacks. The passing seed-2
  native-shape witness is recorded in REIMPLEMENTATION.md, "The battle's
  teardown"; keep that distinction when changing the default policy.
- **When the confirm press lands in Atropos's scene, native shape against default.** The scene on `$10C` polls confirm with event command `$31` (`$C0:66A5`) between lines with no box open; the `motherbrain` route now presses through it, but why the native-shape build's timing puts the dialog's last press before the poll is untraced.
- **An ending's menu script sets the sprites' sizes by `$2101`.** The menus'
  object scripts' byte command `$13` names its cell by address, and an ending
  writes the sprites' size and tile-place register through it; the port hands
  the write to the SNES escape `vid_snes_write_register`
  (`menu/script_bridge.c`, `set_console_byte`), which only the SNES backend
  has. A build without it needs the script's write as what it means: decode
  the scripts as an asset so a command naming `$2101` becomes a sprite
  setting (`VidObjConfig`) the service takes, and drop the escape.
## Port and review follow-ups

- **Prune the evidence cache.** The shared store, `build/evidence-cache` in
  the main checkout, gains a set of entries for every ct_dev binary any
  worktree gates and drops none; keep only the newest few binaries'.
- **Narrow the evidence cache's key.** A dc-evidence movie's result is keyed
  on the whole `ct_dev` binary (docs/PORTING.md, "Declarations liveness can't
  prove"), so any change to the C makes all 84 movies again (about 48
  minutes). A sound narrower key needs to know what C a movie's runs
  executed: `ct_dev` would record the registered entries each run calls and
  the key would take the objects those reach (their symbol references, with
  `game/registry.c`'s table cut and its time model kept), plus `runtime/`,
  `dev/` and the generated C whole. Until a run records that, the binary is
  the narrowest key that can't go stale.
- **What the data teardown's review left.** `$CE:E326-$CE:E332` writes
  `$030C`/`$0310`/`$0314` absolute indexed by X, within the unpacker's
  uncompared `$0309-$0316` range. Establish that its X and data bank cannot
  reach those cells, or narrow the excluded range. The map includes unmapped
  gaps there; absence of a named variable is not proof of no reader
  (REIMPLEMENTATION.md, "The data subsystem's teardown").

- **What the whole-library fixes' review left** (not blocking):
  - `tools/uncompared_evidence.py`'s `compared()` reads only the first row
    for a name; aggregate duplicate names when judging the remaining
    subsystems' isolated comparisons.
  - `dev/fallbacks.c`: a nested or yielded invocation can certify the outer
    one and cancel its open count; count each invocation by its own frame.
  - `ct_dev`: a halt during the comparison tail doesn't set the exit code.
- **What the field port's review left** (rated "mostly", not blocking):
  - `game/menu/display.c` `send_the_frame` returns a value only so its
    wrapper can put it in Y (line 706); the sibling call-outs beside it do
    the same. Return what the caller needs, or nothing, and leave Y to the
    entry wrapper's contract.
  - `game/field/screen_setup.c` `fade_in`'s `shown` out-parameter is a
    register leftover; return the brightness the fade ends at instead.
  - `game/core/input.c` builds a return frame inside the logic rather than
    in its entry wrapper, and `AssignedSet` (`input.h`) is an enum nothing
    takes as a type.
- **The carry chains kept by the owner's decision** (review-port item 12 lists
  them, with the dates they were accepted). Each still rates a file down by the
  rubric's letter and was kept because the original never clears the carry
  between its adds, so the spilled bit moves something the player sees and
  shadow mode and fuzzing compare it:
  - `game/scene/stars.c` (star positions, already isolated in `sum_after`),
    `race_rules.c`, `race_projection.c`, `race_camera.c`: the race's and the
    star fields' positions;
  - `game/scene/curtain.c`: the curtain's adds and subtracts that take in the
    carry before them;
  - `game/scene/random.c`: the add with the carry fed back in;
  - `game/battle/screen_sides.c`, `screen_window.c`, `screen_motion.c`: a
    window's edges and the screen's motion, a column off where the bit spills;
  - `game/menu/epoch.c`'s `on_the_screen`: y from the subtraction's no-borrow;
  - `game/field/line_effects.c`'s `fill_wave_scroll`.
  Found but not yet decided (not on item 12's list; ask the owner):
  `game/overworld/setup.c`'s `character_tiles`, whose doubled byte's top bit
  is added back in as the original's rotate carries it, so an empty place's
  byte (top bit set) reaches round the bank (found by chunk 11's review; it
  was on main before that chunk).
  The follow-up for each is item 12's own form: work out positions in plain
  arithmetic and put the spilled bit in one named function per file that says
  what the player sees (the off-by-one it causes), so the chain leaves the
  routine's shape; `stars.c`'s `sum_after` is the model. Ask the owner before
  starting: the decision to keep them stands until then. Natural times are when
  a file is next reworked, or when the byte-exact gate retires and the
  behaviour can be kept by the named function alone.
- **What `tools/portsmell.py` found when it was written** (its baseline,
  `tools/portsmell_baseline.txt`, is the list; each line leaves it as the work
  lands):
  - 20 `source` lines in `re/ram.toml` cite files that no longer exist
    (`game/battle/stats.c`, `math.c`, `gauges.c`): rubric item 25, a map fix.
  - 15 call-outs reach one of our own registered ports through the machine
    (`game/menu/epoch.c` 7, `game/menu/display.c` 4, `game/battle/display.c` 2,
    one each in `scene/boot.c`,
    `scene/frame.c`): rubric items 22 and 61, each to become a C call behind a
    helper that tests the guard (`event_call_bridge`'s shape).
  - 2 enums nothing takes as a type (`AssignedSet`,
    `DialStep`): rubric item 33, type the parameters that carry them.
  The four bridges' `s->cpu.pc` comparisons are on the baseline with a reason:
  asking whether a call-out returned to its site is not entry identity.
  - 8 flag notes inside a loop or a game branch (rubric items 93, 105):
    battle's `effect_amounts.c`, `enemy_turn.c`, `member_stats.c`,
    `member_turn.c`, `rewards_techs.c`, `tech_records.c`, event's
    `movement.c`, scene's `frame.c`; each to become
    one leaves derivation from the final state. Three more are on the baseline
    with a reason (a wrapper's entry state for a call-out, a shared wrapper's
    exit, the scene interpreter's per-command derivation).
  - 9 return frames pushed by hand outside a bridge (rubric items 85, 101) in
    battle's `animation.c`,
    `main_loop.c`, `core/input.c`, `menu/cursor.c` (3) and
    scene's `frame.c` (2) and `script.c`: register the inlined subroutine as
    its own entry, or port the call-out that needs the frame.

- **Scene's follow-ups from its targeted re-review** (merged with these
  accepted; `script.c`, `sprites.c` and `title.c` rated mostly):
  - the `run_*` rows read operands by position (`operand16(c, 3)`), and
    `(operand8(c, 1) & 0x1E) / 2` is an unnamed "source register" decode:
    decode each command into named parameters;
  - `CHANNEL_PAGES`/`CHANNEL_PAGE_BYTES` restate what
    `scene_program_page_of` and the map already know;
  - `sprites.c:260`'s `0x9801` is unnamed, and the comment doesn't say where
    the fixed `$98` comes from;
  - `title.c:332`'s `move_logo` sets channel 0's script through the raw
    accessor: export a `scene_channel_go_to` from `script.c`.

- **What core's rewrite left** (history/reimplementation-phase3a.md, "Phase 3a: the controller off the
  machine" and "Phase 3a: the rest of core"):
  - what core's review rated "mostly": `palette.c` passes one animation type
    to stand for a family (`ROTATE_DOWN` for both rotations, `FADE_TO_WHITE`
    for every fade, pulse and restore) where a `RunShape` belongs; the held
    level is read out of the period byte in `fade_step` where storage helpers
    belong; the scratch cells are named for one of their uses
    (`SCRATCH_GREEN` also carries a working colour and a frame count); and
    `run_end` uses `native_uncounted_*` where the rest use `charge.h`'s pair;
    `palette_frame_table_entry` takes any long address where an offset into
    the set stream may do (or the accessor says why not);
  - map fixes: `tile_animation_tables`' steps and sources are words the map
    declares as bytes; the block-move stub's two bank bytes want fields of
    their own, and `memory_move_stub` a name for what it does.

- **What battle's rewrite left** (history/reimplementation-phase3a.md, "Phase 3a: the battle off the
  machine"). It waits for its qualitative review (`.claude/skills/review-port/`).
  - Preserve the screen script's excluded-state contracts when changing storage
    (REIMPLEMENTATION.md, "Phase 3, battle: the screen script's shapes"), none of it a
    call the library makes: the drift `$D7` where its amount's high byte,
    the cell `$46` (whatever the last effect left there), isn't clear, or its
    operand names an amount past the six; the centering on the corners
    (`$AD`, `$B4`) where the medians' slopes are more than half a word
    apart, which the original's bit-serial division loses a remainder bit
    on; a circle, square or triangle whose lines would run past the window
    table's end on an odd frame, or whose run of worked rows starts before
    the first or ends past the last; a poke at the script's own place. Decide
    each in game terms before the storage leaves the RAM image.
  - The direction between two positions (`$CE:E7CF`, `battle/screen_between.c`),
    and a triangle's center (`$CD:21C0`, `battle/screen_triangles.c`, whose
    division leaves the last bit its scaling shifted out as `$80` over
    `battle_party_defeated`),
    are worked out in the cells `$23`-`$2A`, over the turn slot's high byte,
    the party's defeat, the random cells and the wide arithmetic's first
    word (`battle_direction_work`), and the port writes them plainly as the
    original leaves them. Find whether any of those is read before it is
    written again after a band; if none is, the writes are scaffolding.
  - The screen's start (`$D1:F1B0`) clears cells no routine of the battle
    reads (`battle_start_cleared`, `battle_start_cleared_byte`, the bytes after
    `battle_message_by_id` and `battle_frame_advance_count`) and keeps the
    layout it starts in where nothing reads it back (`battle_start_layout`):
    find a reader (the field's code, a script's poke) or call them dead.
    `battle_restore_keeps_screen` is read and never set but there.
  - The screen script's model triangles (`$BA`, `$BB`, `$BE`) clear the low
    byte of `battle_turn_slot` ($22): the original hands its rotation the
    triangle's size as a word in `$21`-`$22` (`battle/screen_shapes.c`,
    `clear_the_turn_slots_low_byte`). Find whether any turn reads the slot
    after an effect draws a model; if none does, the store is scaffolding.
  - Meanings not worked out: `battle_ended_effect_object` (`$CD:0024`'s
    object, from `$CF:FE76`); the screen script's special effects that no
    movie shows (`battle/screen_specials.c`: the wedges, the angles'
    straightening, the sprites in front, the layer's bob up, the bands coming
    up a level, the attributes' clear, the background's cycle), named from
    what their code does; `battle_tech_records.menu_detail`; the setup's
    `battle_start_mark`, `battle_menu_mark`, `battle_action_queue_end`,
    `battle_menu_slot_mark`, `battle_list_cursors_shown`, the first ten
    `battle_number_marks`, `battle_slot_draw_bits` and the sprite drawers'
    page's unnamed bytes; `battle_enemy_rule`'s rules 1 and 2;
    the rest of
    `battle_fight_flags`; the unused rows of the script commands'
    per-combatant and per-effect cells (`battle_script_command_cells` at
    `$A171`, `battle_effect_spare_cells`), `battle_walk_cleared` and the
    walk's count at `$A166`, which nothing reads; the five window tile blocks the frame
    loads and what reads `battle_block_uploads_made`; what the marker sprite
    is to the player; why the upload lets enemies in slots 4 and 6+ through.
  - Quirks of the enemies' scripts reproduced on purpose (history/reimplementation-waves.md, "Phase 3,
    wave 2: the enemies' minds"): the test of a stat's value goes on from a
    candidate to the one at twice its slot in the list; the walk toward a
    target measures its reach against a box the multiplier's leftover picks,
    with a radius from a cell the walk doesn't set; a condition's target taken
    from one marked candidate is battle slot 1 where the first isn't the one
    marked; the selector of the enemies with a status in their third status
    byte (`$C1:A8C5`, which no script uses) stores its A over the cell it keeps
    its place in the stat blocks' table in, and reads the blocks that leftover
    gives; the draw among four leaves the script's place on a byte it stepped
    over until the choosing puts it back; a condition that finds nobody holds
    without choosing for some kinds and fails for others.
  - An accepted divergence of the enemies' scripts: narrowing the candidates
    by the aim (`$C1:ADA1`) never leaves its loop in the original where every
    candidate is marked, and the game would hang; the port takes that
    candidate. And the choosing's cells (`battle_script_place` and the rest)
    are written when a choosing ends, not as it reads: the walk's stray box
    read (`$C1:2C02`, which no script asks for) lands on them on a few fuzzed
    states (1 of 3,751 for `$C1:938D`, 3 of 4,942 for `$C1:AFD2`, seed 1),
    where the original reads the place mid-condition.
  - Quirks reproduced on purpose, for a native build to decide on: the chase's
    angle index spilling into `battle_numbers_rising`; a combatant of 1-14
    parts copying eight tile groups; the homing jump lifting twice; an
    afterimage past the right or bottom edge drawn back on screen; the party
    step keeping its list places in the scheduler's entry 0; the weak pose
    restarting frames every step; the pose's restart for an enemy writing past
    the party's frame table; the colour rotation from colour 0 storing a bank on.
    The animation script commands' (history/reimplementation-chunks.md, "Phase 3, battle chunk 9a"):
    the waits on the other targets leaving their mark set (`$03`, `$26`) and
    `$04` waiting on the first target only; `$26` on an effect channel looping
    the animation and running its second operand as a command; the shadows'
    list starting at channel 1's subject and the rotation's at channel 3's;
    hiding a channel's own combatant clearing its off-the-plane bit; bringing
    back, showing an effect and a shadow counting up; the glide's equality
    test; stopping a trail setting its periods from stale work cells; the
    restore after a flash without the rule's test the flash has; the blink
    running itself again in the frame it turns to the flash. Their reads and
    writes past a table, through `_unchecked` accessors (a variable past the
    27, a subject or an anchor named by an operand, an effect record found in
    a byte, an effect animation past the 16), reach the next variable only
    while storage is the RAM image: a native layout needs a decision for each.
    Chunk 9b's (history/reimplementation-chunks.md, "Phase 3, battle chunk 9b"): the middle of the
    first three subjects taking its Y from the sum of their X; a channel's
    line or jump going past the length its last move kept, whatever command
    it is at; the other targets' forms -- lines setting every target off and
    going on at once, spirals out setting them off without marking them
    moving, orbits going on only when none has ended, a chase's or an orbit's
    own heading taken from the first target's for the rest, the jumps and
    throws doing nothing -- and the last of them leaving the work cells as
    the original's loops do; the throw's rise for an anchor reading its speed
    at twice its number (past the movers for the last four); the walk's
    search ending at a ring holding cell 0. Their reads past a table where a
    channel's subject is none (`$FF`), through `_unchecked` accessors: the
    places that take a channel's subject or a mover's heading, the place 24
    pixels along a subject's angle, a subject's facing
    (`battle/script_places.c`), and the weapon sounds' pan, from where
    channel 0's subject stands (`battle/script_sound.c`, `combatant_x`).
  - The commands a row's needs leave to the original that the game reaches
    (chunk 9b): the endings' scripts run moves on a channel whose subject is
    none (`$FF`), the original's indexed writes landing past the movers'
    tables (82 calls of the jump to a place in `endings-beyond-time`, and the
    9a commands' as before); a chase or an orbit at a place whose working out
    overwrites the operands it reads back, and the spiral that stops at
    others on the other targets' channel, whose loop takes its place in the
    list from the overlap test's answer, are left to the original too (no
    movie reaches them). The jumps to the script's point (`$99`, `$9B`, `$9D`)
    lift by whatever `$90` (`battle_jump_lift`) holds, a work cell the
    original shares: most often the 6 the animation slots' loop leaves there
    each frame, but the last jump's own lift, an area search's side, the tech
    list's last tech, a blink's place in the list or a walk's way back when one
    ran since (Spekkio's fight in `battle-tech-sweep-spekkio` reaches it). Every
    writer leaves it plainly through `battle_leave_jump_lift` (`battle/battle.h`);
    what the lift should be in game terms, once the storage leaves the RAM
    image, is open. The throw's last step `$C1:7364` is
    entered by `$D4`'s handler once its wait runs out (`$C1:734F`); where the
    original runs `$D4` its guard turns that entry away too (180 calls in the
    library). Each wants deciding in game terms before the storage leaves the
    RAM image.
  - The accepted divergences, each at its routine: the drawing pass past 128
    sprites (`$CF:E83A`), a frame's graphics built over the direct page's cells
    or the stack (`$C1:1C55`).
  - What an effect's working out still leaves the machine as scaffolding
    (history/reimplementation-chunks.md, "Phase 3, battle chunk 2"), for the party's code and the
    showing, which are the original's: the carry the long multiply keeps in
    its second number's cell (`battle_leave_multiply_carry`, at eight places in
    the work and the enemy's turn, each the carry of what came just before a
    multiply that may be the last), the numbers each kind hands on in the
    direct page's cells (`EFFECT_ARG_1`-`_6`, $0E and $10), and the kinds'
    constant carry and Y at their entry. The adding up of the hits' numbers
    reads the first set's last kind before it adds (`first_began`) only for
    its carry's leave, and `take_next_target` exists only to leave the carry a
    target after the first is taken with: both go with the carry cell. Each
    goes when what reads it is a port: the showing's step. The entries turn away a drain where `$1B` isn't clear, the
    byte the original reads its stat's offset's high byte from; the party's
    code before it is where to show the game always leaves it so.
  - What the effect work still says or does that the original's shape put
    there: `apply_modifier` ($C1:EB1C) multiplies the modifier's number by 3
    and throws the product away (the original's record lookup, whose cells
    only the multiply's scaffolding reads; find the reader or drop it);
    "restarts its script" is still how
    `menu_window.c` (the member who can't act) and `combatants.c` (the status
    seen) name confusion, and `re/ram.toml`'s `battle_status_script_seen`
    says so too: say confused.
  - Bit 4 of a party member's action record's choice (`CHOICE_HELD`,
    battle.h): a knocked-out member's choice loses it (`combatants.c`), and a
    choice of it alone leaves the member's turn as it is, its timer and record
    untouched (`member_turn.c`, history/reimplementation-chunks.md, "Phase 3, battle chunk 5"). Which
    menu command sets it, and what the player sees, is to work out; name it
    then.
  - The statuses' timers (history/reimplementation-chunks.md, "Phase 3, battle chunk 3"): the first
    status byte's `$10` (a point of HP lost each time its timer runs out) and
    the fourth's `$20` (5 MP back every tenth) are named for what the battle
    does with them; what the player calls them is to work out. Timer kinds 6,
    7 and 11 keep neutral names (`BATTLE_TIMER_6`, `_7`, `_11`): 6 and 7 time
    no status the battle sets, 11 follows the third status byte's `$02`, and
    none does anything when it runs out; name them once what they are is
    known. The slow's timer wears off without taking the slow away, and the
    MP's timer halves for kept haste alone: both the original's. No movie
    reaches the slow's or the MP's; they were fuzzed only.
  - Number commands `$54`/`$55` would read records past the 44 the map
    declares; no movie reaches them. The scripts' advance (`$C1:75BB`) is
    written twice, in `numbers.c` and `scripts.c`;
  - what the battle menu's input review left (none rated translation):
    `menu_input.c` uses `menu_nobody(slot - 1)` as an underflow test, copies
    `open_items`' frames instead of one `put_item_page_frames`, and steps
    `battle_targeting`, `attack_by_shortcut` and `item_list_changed` with
    `+1`/`-1` (named set/clear); `area_search.c` writes
    `set_battle_multiply_factors` unmarked though nothing reads it, and
    exports `battle_area_search_carry` from the logic (beside the entries);
    `AreaAsked.flag` wants the name `short_beam`; `window_data.h`'s
    `battle_stat_block_place` reads past the table for the menu's nobody;
    `CHOOSING_FRAME_LEFT_A` in `menu_window.c` repeats `menu_input.c`'s entry
    derivation; `menu_lists.c`'s `ITEM_WORD` is raw font bytes;
    `target_choice.c`'s circle sizes `0x09/0x10/0x19` read better as 3², 4²
    and 5²; `field/walk.c`'s guard comment should say why
    `right_side(at.x) + step.x` near `$FF8F` never feeds a carry the port
    drops.

  - what an action shown's review left (none rated translation): cells
    incremented where they are bools or known 0 (`action_showing.c`,
    `pose_looks.c`, `frame_step.c`); `damage.c` keeps tech points as a word
    and a byte with a hand-made carry (a wide accessor), and tests
    `& 0x8000` where it means below nothing; `pose_looks.c` works out the
    palette's colour place again (as `from_field.c` and `object_motion.c`
    do), and names `POSE_HELD` by its byte rather than its status;
    `action_animation.c` repeats `battle_decoded_tiles`' address as
    `DECODE_BUFFER` and names action `0x37` only for its effect;
    `battle_poses_round_done` is read back as a loop flag where
    `battle_go_round_the_poses` could return it; `frame_step.c`'s queue
    shift (place 3 from place 2) doesn't say what the player sees, and it
    calls a guard then calls out by hand rather than through `battle_run`.

  - what the refusals batch's review left (none rated translation): the
    wear-off rule (restart from its length, ten runs, stop and clear) is
    written in both `timers.c` and `status_timers.c` -- the timer storage
    should take a nobody-aware owner so `timer_ran_out` serves both, and
    clearing nobody's statuses is a second stray place through
    `combatant_lose_statuses`; `enemy_movement.c`'s
    `where_the_original_finds_nobody` repeats what `combatant_x`/`_y`
    already read, and `takes_no_target` admits every non-combatant where the
    comments say `$FF`; the `goes_on_into` guards re-decide what entering at
    that address implies; `play_nobodys_animation` takes a parameter only
    ever `SCRIPT_NOBODY`; the registered `event_cmd_set_solidity` names an
    operation now called `set_contact`; `charge.h`'s comment carries a
    history note that belongs in REIMPLEMENTATION.md.

  - what the triangles, circles and special effects' review left (none
    blocking): `screen_triangles.c` works out the bit-serial divider's
    leftover (`last_bit_scaled_out`, `left_of_the_dividend`) outside
    `ORIGINAL_EFFECT` and keeps it plainly in `battle_direction_work`, landing
    `$80` on `battle_party_defeated` -- find the reader or prove the cells
    dead, then move it to a leaves helper; `screen_script.c`'s
    `draws_a_sized_triangle` lists opcodes in the runner to decide which
    scaffolding to leave (a row column, or `draw_triangle`'s own
    `ORIGINAL_EFFECT`); the shared parameter list now holds both `kind` and
    `circle_kind`; `screen_specials.c` names the wedge corners as bare
    coordinates, splits an animation position with `>> 8`/`& 0xFF`, and reads
    the effect's sprite records by offset; `display.c`'s `move_run_values`
    joins byte accessors into a word; `screen_triangles.c` tests
    `slope & 0x8000` for a negative slope; `screen_shapes.c`'s `model_fits`
    re-turns and re-sizes the model to predict whether it fits; the
    `battle_run_screen_scripts` and `battle_run_screen_channel` vblank-tail
    exceptions lost their "at most N clocks" bound.

  - what the screen effects' second half's review left (none blocking):
    `frame.h` exports `battle_leave_arrangement_registers(Snes*)` from a
    logic header (to entries.h or the leaves); `screen_setup.c` writes the
    window settings as register values (`0x78787878`, `0x05050505`, mode 9)
    where layer bits and named fields would do, with an unnamed
    `short_window_down` and `0xFFDF`/`0xFFFC` for -33 and -4, and its
    starting resets read as flat setter lists; `screen_lines.h`'s
    `line_edge` takes `unsigned` where the others take `TableLine`;
    the screen language's shared list gained `window` and `quad` as bare
    bytes converted at each use and `stray_bits` only for a guard (rubric
    116, worse by those).

  - what the battle's start and leftovers' review left (none blocking):
    `effect_animation.c`'s `TakenFrame` is returned and carried through the
    loop only for `leave_taken_frame` -- work it out afterwards from the
    effect's state; `objects.c`'s `due_after_count` re-runs the due search to
    predict the next object where the guard could state its invariant, joins
    the stride's cells by hand, and stores a bare 0 for "not animating";
    `nearest_enemy` is in both `opening.c` and `animation.c`, and
    `combatants.c`'s `come_back` repeats `status_timers.c`'s
    `set_up_status_showing` (with `SHOWN_AS_TECH` beside `ACTION_TECH`);
    `from_field.c` names fight-flags bit 1 locally where the map says it
    blocks the stance poses, divides a byte place by `0x50`, and its member
    palette arithmetic doesn't say the byte is in attribute form; `opening.c`
    writes `LEFT_MEMBER` as address arithmetic and 1/0 for bools;
    `enemy_turn.c`'s unreachable `no_turn` row's comment is wrong;
    `m_return_to_caller_long` wants its own comment; `burndown.py` should
    report code neither reached nor started by an `unreached` entry rather
    than drop it; `hints.toml` should note the dead veneers at
    `$C1:FF53`/`FFAE`/`FFCC` beside `$C1:C09A`.

  - what the screen script language's review left (none blocking after its
    fixes): `screen_script.h`'s language-wide parameter list forces generic
    names and bare `uint8_t` (`value` means about ten things, `which` four,
    `kind` three), so `show_layers` casts to `LayerSetting` and
    `set_frame_switch`, `wave` and `fill` fold several operations into one
    switch -- name the parameters, declare the enums the table uses, one
    operation per case; `run_screen_scripts` and `step_channel` keep the
    scroll mode, the ended-channel count and the restore request in `Snes *`
    bodies, and the script cursor goes through `battle_screen_script` every
    byte (needed now only for the commands a row turns away and for `$C1`'s
    wait); `saved_shape_offset` and `script_palette_slot_offset` work in byte
    offsets, and the put-aside `array_starts` table carries a record's
    layout; the BG3 line-scroll flag written `(bg3 & 0xFF00) | value`;
    `battle_screen_command_ready` takes the carry for every command only for
    the jitter; the old `$D1` entries' positional `screen_command_operand`
    wrappers duplicate the rows once every row is C.

  - what battle's review rated "mostly" (no file rated translation):
    - the reactions (`main_loop.c`'s `react()`) hand their choosing to the
      enemy's turn through the choosing's cells and take its outcome and the
      script's place back from them, and push the acting enemy on the machine
      stack under the turn, whose call-outs compare it: the turn's results as
      values once its call-outs are ports;
    - one fact defined several times: "out of the fight", the first combatant
      object, the graphics kinds
      (`kind != 3` in `sprites.c` against `kind < 3` in `animation.c`, which
      disagree on kind 4), two `battle_sine_magnitude`s over different
      tables, one ROM table under two names twice, the battle's VRAM places
      as literals in `display.c` and `uploads.c`, `TrailPoint` declared two
      ways, `SPRITE_BYTES`/`NUMBER_DIGITS`/`enemy_slot` copies;
    - entry and map names that disagree with the operations
      (`battle_step_orbits`/`step_drifts`, `battle_screen_wipe_from_left`/
      `wipe_diagonally`, the mover tables as `battle_effect_*`, and others);
    - the cursor bridges in `menu_window.c` keep the target's side in a
      variable named `carry` and their call-out-free logic in the `Snes *`
      body: derive the carry in `window_leaves.c`; the tech rules and the
      combination's calling off belong in a rules module, not the window;
    - ROM and record accessors that take byte offsets the callers compute
      (`setup_data.h`, `battle_sprite_layout`, `battle_palette_frame`, the
      effect sprite record by `from + 2`), and inline ROM reads in
      `main_loop.c` and `scripts.c`: index-taking accessors returning records;
    - `x + 1` written to cells that mean a bool or a load step;
    - `NATIVE_TIME_AS` takes a `Snes *`, so the draw pass, `step_movers` and
      `step_enemy_group` take one only to name their parts' time: a charge
      hook reachable from `GameState` (a runtime change);
    - map fixes: the word cells of which one byte means something
      (`battle_repeat_delay`, `battle_window_layout`, `battle_frog_slot`,
      `battle_graphics_object`, `battle_bitmap_set`, `battle_magus_slot`,
      ...), `battle_target_display`'s fields, and the stale `source` lines
      naming functions that no longer exist (`battle_run_queued_scripts`
      and about fifty others);
    - from the enemies' minds' review (history/reimplementation-waves.md, "Phase 3, wave 2: the
      enemies' minds"): `enemy_turn.c` runs the actions' turns, which are C,
      as sibling entries through `battle_run` and the dispatch's registers,
      and they give their results (the outcome, the follow-up, the message
      kept, how far the script was read) through the battle's cells: a C
      dispatch returning them once the turn's call-outs are ports;
      `targets.c`'s entries named for bytes (`battle_select_members_with_02`,
      `battle_select_slot_3_again`) and the emulation of `$1F`'s stray
      pointer, which no script reaches; `positions.c`'s `walk`, still the
      original's steps; `rules_data.h`'s accessors taking byte offsets; the
      `_by_wide_place` stat accessors, which name the original's indexing
      rather than what is read; and the meanings of the action record's
      bit 4 (`ACTED_BIT_4`) and the flags action's second do-nothing bit;
      from its re-review: the carrying-out side (`enemy_actions.c`'s turn
      parts) still decodes operands by position, where the choosing's rows
      decode them by layout; the flags action's bits are a `uint8_t` tested
      in turn, not a decoded choice; the pose kinds ($04, $0D) are unnamed;
      and the cells a candidate past the combatants can be read from are kept
      plainly only where the conditions write them ($08, $0A-$0B, $0E:
      battle.h), not the selectors' other scaffolding ($02, $0C-$0D), which
      such a read could reach as well;
    - `nmi.c` is named for the interrupt, and the battle's two vblank
      handlers sit in `nmi.c` and `display.c` without pointing at each other;
      `frame.h` describes `BattleScrollMode` by what is still the original's.

- **What menu's rewrite left** (history/reimplementation-phase3a.md, "Phase 3a: the menus off the
  machine", and "Phase 3a: the menus' follow-ups and the Epoch"):
  - the load screen's member rows keep their own place, where the original
    reads it back from its cells after the member row's window list and the
    portrait have run; both are C now, and write those cells only as the
    original does, so the two part only where a list's strings write over the
    cells (none of the game's do);
  - the map's strips are built in the upload buffer where the game keeps it:
    `map.c`'s guards hand a buffer outside work RAM above low RAM, a plane
    whose top-left tile is off the map, or a queue without room for a
    scroll's strips to the original, none of which the library makes;
  - what the Epoch's review rated "mostly" (`menu/epoch.c`, `top_layer.c`):
    - the gate window's fill walks its edges through the `menu_gate_edge_*`
      cells, which nothing else reads, masking a 16-bit declaration down to
      the byte that means something: walk local edges and leave the table
      once through `ORIGINAL_EFFECT`, and narrow the declarations in
      `re/ram.toml`;
    - `turn_corners`/`multiply_signed` keep `SignedProduct` and `last`
      locals only for the leftovers, and `on_the_screen`'s `wrapped` is the
      add's carry for x but the subtraction's no-borrow for y, which its
      comment and name get wrong: one named quirk per axis, and the
      leftovers derived after the call;
    - `WindowDrawn` is filled inside the corner loop for the leftovers
      alone; the middle lines are read and filled in bank `$7E` where the
      rest go to `bank`, without a comment;
    - raw button bits and `input_mapped(g, 0/1)` where `core/input.h`'s
      `BUTTON_*` and `ASSIGNED_*` belong;
    - `start_dial_objects` hands each digit its period through the running
      object's own slot and restores it; set the started slot's instead;
    - the dial's and the map view's screen setup (`show_the_time_dial_with`,
      `show_the_map_view_with`) is about 25 shadow writes in the original's
      order: one `VidScreen` value each would say what the screen is;
    - `draw_map_tile`/`draw_map_picture` return the last group only for the
      leftovers;
    - `upload_gate_rows` accepts a count of 0 and a table anywhere; the guard
      should refuse what the game never passes;
    - `sine()` is written three times (`epoch.c`, `top_layer.c`, `wave.c`),
      and `OBJECTS_HELD`/`SCREEN_OFF` twice;
    - sign-bit compares, an uncommented `_unchecked` read in `group_pixel`,
      and the numbered entries `menu_object_window1_lines_2`/`_3`.

- **What event's and field's combined pass left**
  (history/reimplementation-phase3a.md, "Phase 3a: event onto the field's
  objects"):
  - Decode a `Location` in the command row, rather than two words in
    `event/locations.c::location_of`; type comparison operators, urgency
    and flags, and name face/animation commands by meaning.
  - Share the edge-upload table between `field/nmi.c` and `display.c` once
    `dontcare_check.py` can follow header tables. Name staging buffers and
    stream layouts in the map, then pass buffers rather than bank addresses
    (`layers.c`, `sprites.c`, `graphics.c`, `display.c`).
  - Give scroll, object view, line effects and event visibility one camera
    vocabulary. Separate automatic scroll and vblank sends by purpose;
    introduce a dialog-window type and explain `field.h`'s responsibility.
  - Keep register derivation out of `line_effects.c::leaves_after_send`'s
    logic; retain only the rasterizer compatibility its callers need.
  - Consolidate `move_to_tile`'s slide/arrival logic; store the tint as
    colour semantics; compute guarded bit masks instead of reading a table.
  - Share the four sound bridges' parse/call shape. Finish removing callback
    and scratch coupling as the remaining machine forms leave.

- **Shadow mode pairs a bridge's calls in order.** Optional call-outs must
  be predicted by the guard. Pairing by site and routine would remove that
  restriction for still-gated bridges. The field's `field_view_changes` and
  `every_draw_is_own` now also enforce C-path state bounds; do not remove
  those bounds merely because the field's comparison retired.

- **What only the original's memory has, past the object tables.** Before
  splitting field storage, define the sprite high table's overrun, the fifth
  sprite's offscreen Y and the opening's word clear into the next slot
  (`sprites.c`, `objects_start.c`; history/reimplementation-phase3a.md).
  Event commands `$24`, `$A9` and waiting `$07` still require a present party
  object; preserve those contracts or adopt `Someone`'s absent-member state.
  `field_leaders_place` reads the slot past the tables when no leader exists
  during a load or attract handoff. Decide whether native storage preserves
  those bytes or uses the location's own start; a field's normal start later
  replaces that place. The visible walking/search case is "Nobody leading".

- **What battle's chunk 6 left** (history/reimplementation-chunks.md, "Phase 3, battle chunk 6"; its
  review's follow-ups):
  - one accessor for the other items' battle records (`$CC:0ABC`):
    `equipment_data.h`'s `item_in_battle` and `effect_data.h`'s item-use
    reader each read the record, and give its bit 7 two meanings;
  - the equipment's second records decoded in `equipment_data.h`: the boost
    and affinity bytes as a typed record, not a place and byte offsets
    (`BOOST_BYTE`, `AFFINITY_BYTE`);
  - the dead write of `TABLE_AT` (`STAT_SETUP_SPARE_TABLE`) in
    `member_stats.c`: find what, if anything, reads `$0E` after the stats'
    setup, and make it scaffolding or say why it is kept;
  - the multiply's carry notes (`member_stats.c`, `tech_records.c`,
    `item_record_place`) derived once, as one leave, and `member_turn.c`'s
    `lengthened` returning its result rather than through an out-parameter;
  - the MP cost cut's accessory constants shared between `party_work.c` and
    `tech_records.c` (`QUARTERS_COST`), `multiply_out_tech_place` with
    `fill_triple`'s multiply, and the tech records' layout (`TECH_RECORD_BYTES`,
    the first double's and the triple's record) taken from the map;
  - the gold's accessors named for what they are (`party_gold_low`/`_high`
    as one 24-bit value in the map);
  - battle.h's list of `$00`'s writers brought up to date with the setup's
    (tech_records.c, member_stats.c, inventory.c's item list);
  - `item_record_place`'s `none` parameter: the caller's place for an item
    past the recorded ones is the original's leftover X; say so at the call,
    or give the item no record.

- **What battle's chunk 7 left** (history/reimplementation-chunks.md, "Phase 3, battle chunk 7"):
  - the attack's second part is a byte of bank CC at the member's block's
    place, where the original means the weapon's record (it has just found
    it by a multiply, then reloads the index): a native build decides
    whether to keep the stray byte or take the weapon's (`fight_party.c`,
    `fight_add_up_member_stats`);
  - an enemy whose stat block has bit 0 of `$0A` gets `$40` in its standing
    as its first turn is set (`fight_timers.c`); nothing the enemy's record
    fills sets the byte, so the start never marks one: find whether anything
    sets it later and what the mark is for, or drop the test in a native build;
  - `battle_enemy_animation_work` (`$7E:5E1D`, sixteen bytes): the enemies'
    movements (`battle/enemy_movement.c`) keep the first eight, by enemy; no
    ported code reads the eight after them, which the fight's start clears
    with them: find their reader, and split the name;
  - the bytes the fight's start clears that no traced code reads, named for
    now as spares in `re/ram.toml` (`battle_fight_start_spares`,
    `battle_condition_target_spares`, `battle_effect_record_spares`,
    `battle_enemy_turn_spare`, `battle_timer_tick_spare`,
    `battle_demo_spare`, `battle_fight_start_mark`, the carried weapon's
    marks and spare, `battle_enemy_family_bits`): find a reader or drop
    them with the gate.

- **What battle's chunk 8 left** (history/reimplementation-chunks.md, "Phase 3, battle chunk 8"):
  - the tech points' count (`rewards_techs.c`, `rewards_count_tech_points`)
    pays the battle's points again towards a tech whose cost the points left
    over from the last one don't reach, so a member can learn more than the
    points pay for: a native build decides whether to keep the original's
    count;
  - a level a character in reserve gains is noted in bit 7 of the byte after
    the members' three, which is the first member's techs' byte (no message
    tests that bit there), and counted in the byte after the members' counts,
    whose carry reaches the battle's reaction entry (`rewards_levels.c`,
    `count_level_gained`). The messages (`rewards_messages.c`, `$FD:AD17`)
    never read either: they test only bits 3, 2 and 1 of a techs byte and
    count only the members' three levels, so a native build can drop the
    reserve's noting;
  - the share for the reserve clears `battle_status_script_seen`'s first byte
    as it clears its own cell (`rewards_share_with_reserve`): nothing after
    the battle's end reads it before the next fight's start clears it, so a
    native build can drop the write.
- **What the targeting left** (docs/history/reimplementation-chunks.md, "Phase 3,
  battle: the targeting"),
  its review's findings not yet taken:
  - `aim.c` tests who can take part with its own `in_fight`, `knocked_out` and
    `free_to_act`, beside `combatant_in_fight` and `combatant_cant_act`, adding
    status 2's bits `0x18`, whose meaning isn't worked out: one test, and the
    bits named;
  - the member's turn hands the targeting's original its cells (an item's area
    rules at `$0C`, the action record's place at `$06`): parameters once the
    targeting's original callers are ports;
  - a chosen target of none reaches the knocked-out test with `$FF` for a
    single-target mode, and the reserve tests read the area's end marker and a
    battle slot as enemy numbers (`battle_enemy_standing` unchecked): a native
    storage needs a typed "nobody" for each;
  - the ROM's tables `$CC:1ACB` (a party tech's aim) and `$CC:1BB5` (an item's)
    are described by `aim_data.h`, `effect_data.h`, `menu_data.h` and
    `equipment_data.h` differently, `effect_data.h`'s as not worked out: one
    record type each.

- **Nobody leading** (`game/field/object.h`, `FieldLeader`). An absent
  leader's marker indexes past the object tables; position, facing and ground
  priorities then come from adjacent variables. The screen follows it in
  `late-omen-terramutant` at frame 14887 with a direction held. Confirm
  searches can also read off-map tiles (`event/chests.c::walkable_past_the_map`),
  including with a real leader at Zenan Bridge's edge. Counts 64-127 start
  collision searches beyond the tables (`look_past_the_tables`,
  `met_past_the_tables`). Before moving storage, preserve the necessary
  layout or define bounded game behavior for these reachable cases.

- **What only the original's memory has, in the battle** (history/reimplementation-chunks.md, "Phase 3,
  battle chunk 4", after the review). A build without the original's memory has to decide each:
  - a status's timer of nobody's: the search for run-out timers takes the
    order's empty place (`$FF`) for a combatant and reads its state past the
    kind's row (`timers.c`, `has_run_out`); a status that wears off is worn
    off there as a slot's, its writes past the rows landing in the battle's
    other tables (the evading much better's state is the poison's length for
    the third slot; `timers.c`, `battle_timer_wears_off_past_the_rows`, the
    one place; 4 calls over the library, `gato-battle` and
    `endings-telepod-lavos`), and the other kinds' handlers for nobody are
    still the original's, which the game doesn't reach;
  - an enemy that saw no target (`$FF`) waits on where nobody stands, the
    battlefield's objects' places read past the combatants'
    (`enemy_movement.c`, `where_the_original_finds_nobody`; the Omen's
    battles);
  - a combatant channel whose subject is nobody (`$FF`, the Bucket ending's
    channel for a partner the party doesn't have) plays and waits on that
    slot, its indexes doubled and tripled in a byte into the scheduled lists'
    and the frame lists' tables (`script_poses.c`, `play_nobodys_animation`,
    the one place), and waits as long as the byte past the animating table
    says;
  - a place from `$80` names the kind of the one `$80` below it, its subject
    `$80` further on, past the subjects' table (`script_places.c`,
    `decode_place`; the Beyond Time ending's jump to `$80`);
  - the palette's send from the vblank handler in bank D1, which doesn't set
    the data bank: on code with work RAM's bank its transfer's writes land in
    work RAM (`$7E:2121`, `$7E:420B`, `$7E:4370`-`$4377`) and the palette
    isn't sent that frame (`uploads.c`, `send_palette_through_work_ram`,
    the SNES escape `vid_snes_send_palette_into_memory`); what the
    battle keeps at those cells isn't worked out, and a native build sends
    the palette;
  - a party tech's part with no member: the original reads the slot of the
    id `$FF` past the character slots (`$7E:B2BD`) and ends the tech only
    where that byte is `$FF` (the guard asks, `no_member_ends_the_tech`);
  - after a part that missed, the tech stops where bit 7 is set in the byte
    of work RAM at `$7E:B1F5` plus eleven times the tech, which the original
    reads where the tech's record is meant (`party_work.c`,
    `stops_after_miss`, through `wram_read8`);
  - from the setup (history/reimplementation-chunks.md, "Phase 3, battle chunk 6"): the search for a
    member's triple tech reads the byte after the fifteen learned ones as it
    ends (`tech_records.c`, `learned_byte`); a member wearing an item past
    the recorded ones ($F2 on) has bank CC's first bytes, at twice its slot,
    copied into its block as that item's record (`member_stats.c`,
    `copy_equipment_records`); and the item list's building reads the
    inventory's 256 places, the fourteen past its own included
    (`inventory.c`); from the fight's start (history/reimplementation-chunks.md, "Phase 3, battle
    chunk 7"): a demo battle gives HP and MP to eight characters' records,
    the eighth landing on the techs learned after the seventh's record
    (`fight_party.c`, `fight_load_demo_party`); the double techs' setup reads
    the byte after the last learned double tech as it ends
    (`tech_records.c`); and a weapon made for no character reads past the
    characters' slots for its owner (`fight_party.c`, `owners_slot_bit`);
    and from an action's showing (history/reimplementation-chunks.md, "Phase 3, battle: an action
    shown"): the subjects' list of an action with more than six other targets
    runs past `battle_script_subjects` onto the block uploads' count and the
    enemies' family bits (`action_showing.c`, `list_subject`), and the
    showing's own list (`battle_showing_targets`) over the trails' high bytes;
    and a felled enemy's spoils read its cell (`$0E`) as a word with the one
    after it, and where that one is set its standing and id from that many
    pages on, giving another enemy's experience, gold, tech points and item or
    none (`damage.c`, `felled_enemy`, which reads the scratch cell as the
    original does). The library sets it at ten landings of hits in the Lavos
    fights, none felling an enemy; a native build, which doesn't keep the
    cell, decides it otherwise.
- **What an action's showing left at its edge** (history/reimplementation-chunks.md,
  "Phase 3, battle: an action shown"): the showing's stub (`$C1:BFA4`);
  and the frame waits (`$C1:007E`), which wait on the
  vblank handler and go with the timing service (PLAN.md, "Timing is
  decided"). Poses 1, 10 and 11 have looks (`pose_looks.c`) no status gives.
- **What the battlefield's change left** (REIMPLEMENTATION.md, "Phase 3, battle: the
  battlefield changed and an effect's rows of its tiles"): the rest of the original
  that loads a battlefield, which calls the ports; and
  the map buffer's clear (`$CD:1052`), whose every call in the library spans
  the vblank handler, so its `cost_milli` is an estimate from its loop (the
  original's time over the port's accesses in the shadow runs) the timing gate
  has never measured. The first row set an effect loads takes A's high byte
  as the frame work's wait leaves it (`$CD:1339`); it is 0 in every call the
  library makes, which the port assumes.
- **What the accessor checks' fuzzing left** (REIMPLEMENTATION.md, "The accessor bounds
  checks"). Random states still drive these accessors out of range, where no
  guard turns the state away; no movie does:
  - the tile ahead of a moving object, off the map: `field_start_step`
    `$C0:ACFD` and the event moves that set off (`$C0:4E73`, `$C0:4EC7`,
    `$C0:4F0A`, `$C0:4F61`, `$C0:4FEA`, `$C0:5084`, `$C0:5129`, `$C0:5191`,
    the moves toward an object and the follows `$C0:3546`, `$C0:354F`) read
    `field_tile_*` past the 64x64 tables. Deciding what walking off the map
    meets is the port's; a guard on the objects' positions would do for the
    gate;
  - the line effect tables' builders (`$FD:C2C1`, `$FD:C995`, `$FD:CD0C`,
    `$FD:CFCF`, `$FD:D27E`, and the wave parts `$FD:CC5E`, `$FD:CCB8`) write
    past a table's 87 bytes when the band's half height or a wave part's line
    count is out of the game's range: the guards want the window's largest
    half and the parts' largest counts. The same case reaches
    `field_rebuild_line_effects` through `battle_nmi_alternate`; retain
    that caller when testing the bounds;
  - the event engine reads the script past its buffer (`$C0:5A93`,
    `$C0:58DE`): the opcode byte before the command's guard, and an opening's
    end byte. Treating every offset past the buffer as off the script (not only
    `$FFFF`) is right for the turns, but the openings' hand-over at `$C0:5900`
    then mismatches in fuzzing (254 of 333 states), so the openings' path
    off the script needs fixing first.
- **Accessor-check performance is unresolved.** Two suite runs grew about
  20%, while alternating isolated runs differed by at most 1%. Reproduce the
  scheduling/build difference before attributing the gap to bounds checks
  (REIMPLEMENTATION.md, "The accessor bounds checks").

- **What the jet bike race's review left.** `race_rules.c::drive_johnny`
  uses `race_lane_high` rather than a lane value; `math.c`, `sprites.c`,
  `race_camera.c` and `race_matrix.c` still need the qualitative follow-ups
  recorded in history/reimplementation-chunks.md. Preserve the full program
  image through `$7E:C994` when reproducing old scene fuzz evidence.

- **What the staff roll's review left.** In `staff_roll.c`, separate
  `vram_site` from the graphics records; express the setup through one
  `VidScreen`; name the priority fill byte and palette indices; use typed
  booleans and one palette-copy representation. In `script.c`, give unlisted
  command nibbles an explicit policy and bound a script turn that never ends.
  Low/high-byte fills are tracked under "Video escapes after the retired
  subsystems". Add a `PF_CALL_DC` case to `tests/test_burndown.py`.

- **What the race's second review left.** `perspective.c::start_lines`
  should return its line step; `PerspectiveStart` should name a scene kind.
  Separate the origin tags and continue flag from `race.c`'s graphics rows,
  name the pictures/rows/columns in `crono_blocks`, and share its `on_page`
  predicate with `bridge.c`. `race_matrix.c` still writes
  `scene_scaler_request_state` plainly without an established reader.
  `math.c` passes C operands/results through `scene_call` cells; pass values
  instead. Narrow the lane-related cells behind `with_low_byte` and `& 0xFF`.

- **The time warp's flicker reads past its table.** `warp.c` takes the
  flicker's row 256 x `past` bytes beyond the flicker table
  (`warp_data.h`), `past` being the bottom line's scale left in A's high
  byte, as the original does. It is deliberate and matches, but it reads
  whatever ROM follows the table: a build that loads the table from an
  asset pack has to carry those bytes too, or the warp has to be shown
  never to reach past the table (fuzz the scale's range) and the read
  clamped.
- **What the boot path's third review left** (none rated translation):
  `scene/curtain.c` keeps its cursor in `scene_curtain_table_bank_high`
  where a local would do, copies through the work-RAM port, needs a comment
  at line 135 saying why, and its `sweep` wants a `CurtainSweep` type;
  `menu/line_effects.c`'s `as_the_curtain_leaves_it` rebuilds C and V by
  hand (declare them don't-care if the liveness analysis agrees), and
  `curtain_shown` repeats the scene's rule.
- **What the boot path's second review left.** Use one operation to end a
  scene (`screen.c`, `script.c`, `race_rules.c`), with a byte-wide flag.
  Consolidate the task-specific leaves helpers and the `run` adapter in
  `title.c`; share `PROGRAM_DATA_BANK`/`PROGRAM_BANK`. Rename `CALMS_EVERY`
  as a mask. Give the setup table a column for the title special case and
  names that distinguish the ending modes.

- **What the boot path's review left.** `core/tile_animation.c` threads
  `LoadLeft` through its logic for compatibility scratch. `field/location.c`
  copies its built-in command by raw offsets and retains a machine loading
  path. `scene/title.c` guards repeat multiply predicates. Remove these
  dependencies with their callers; the scene's retired timing rows belong
  to the ledger cleanup under "Scene continuation boundaries".

- **What the time warp's review left** (none rated translation):
  `WarpKind` carries ROM columns (`frame_entry`, `window_site`) beside the
  kinds' length and flicker, used as `PF_AT(...)`, `charge_as(...)` and the
  held wait's return (a parallel dispatch table); `warp_build_circle`
  returns its step count only for the timing charge; `circle.c`'s
  `h->area & NEGATIVE` is a sign test (`(int16_t)area < 0`); `picture.c`'s callers pass origin tags into
  `start_line_effect`, and the plane's tiles are filled byte by byte
  through the VRAM port; `warp_work`'s offsets are reused across files
  under different names (name the cells in the map); a third private
  accessor for the `$C0:F800` sine (one, shared); `TM_LINE_UNCHANGED` is an
  in-band `0xFFFF`, and the SNES backend's comment says the call also
  turns the picture on dark, which the native `timing.c` doesn't.

- **What the endings' review left** (none rated translation):
  `scene/endings.c`'s modes are numbers with names, not the endings they
  are (`ending_mode` returns `unsigned`, `MODE_CAPTIONED_TOO` names
  nothing, mode 14 falls into a `default:`, the header doesn't say which
  ending each mode shows): an `Ending` enum named for the endings, used as
  the type; `set_up_ground_layers`, `show_credits` and `show_last_scene`
  set the screen register group by register group in the original's order
  (the same window sweep four times): build one `VidScreen` each and apply
  it, as `screen.c` does; `UNPACK_BUFFER`, `PLANE_MAP_HALF`,
  `WORKING_PALETTE` and `0xC995` (race.c's `PLANE_COLUMN_COPY`) are
  defined again in several places, one buffer under three names (once, in
  `scene.h`); cells declared wider than they mean and a flag incremented
  (`reset_disabled = 1` aside); scaffolding written through
  `set_scene_race_picture`, an accessor named for the race.

- **What the world map's setup review left** (none rated translation; the
  `menu_epoch_build_map` guard is a real precondition and the main loop's
  call-out unreachable): `overworld_load_the_map_tiles` is called without
  `TIMED_AS(0xC227DE)` though the sends around it are tagged; the
  `doubled >> 15` carry term is 0 for every member byte the game makes;
  `menu_mode_beside`/`menu_random_beside` name a 16-bit clear's second byte
  after its neighbours; `display.c`'s raw shadow bytes `0x09`/`0x17`/`0x13`;
  `epoch.c` still calls `$C2:26A8` through the machine though
  `menu_clear_all_of_vram` exists; `object_commands.c`'s story flags aren't
  named for what they mean; `music.h`'s `region` exists only for the wrapper;
  `terrain.h` exports a carry derivation; `0x7E4000` is spelled in both
  `setup.c` and `main_loop.c`. In `tools/timing_exceptions.txt`,
  `overworld_load_the_map_tiles` and `_load_the_map_again` give "its time
  following the packed data" as the reason, but the unpacker is timed as
  itself and their own work is a few table reads (map_tiles is calibrated
  at 340x): find what they carry -- likely the untagged call above -- and
  make the reasons say it (or the entries leave the file); and
  `overworld_back_from_the_menus` went from 21663 to 343895 cost_milli
  unexplained.

- **What the battlefield change's review left** (none rated translation):
  `battlefield_change.c`'s map build keeps its working state in work RAM
  (`battle_map_cycle`, `_strip_step`, `_strip_cells`) and reads which places
  have a tile with the original's shift-left bit reader (keep them in locals,
  test the place's bit, write the final values once); the battlefield
  accessors in `frame_data.h` take a built bank-and-place address and the
  descriptors' `0x8000` (take the set, strip and tile and return records); the screen effects' settings
  (`$53`-`$61`, `battle_screen_effect_settings`) are written through the
  scaffolding writers `screen_leave_*` under `LEFT_*` names (they are game
  state the scripts read: write them through the settings, named for what
  the effect takes); `MODE_1_BG3_FRONT_16x16`, `BG3_MAP_AT_7C00` and the
  hand-packed tile entries are register encodings (the video service's
  terms), and `BG3_MAP_AT_7C00` repeats `MAP_VRAM`; `flip_tile`'s result and
  `MapBuilder`'s `last_*`/`built_any` exist only for leftover cells;
  `screen_script.c` clears the channels' state with `wram_fill16` at an
  address (name the block in the map).

- **What the attract refusals' review left.** Name the wrapped camera-gap
  arithmetic in `race_projection.c` for the bike positions it computes, and
  drop `horizon_gap`'s `keep_divisor(g, last_divisor(g))`, which writes back
  what the cell already holds.
  Establish readers for `perspective.c`'s line-above-screen cells before
  moving storage; the `$7F` cells need `reuses` in the map. `native.c` sorts
  same-address entries with unstable `qsort`: break ties by table index or
  assert that their guards do not overlap.

- **What the Epoch's, the vehicles' and the screen objects' review left**
  (none rated translation): the objects' loop (`objects.c`) and the
  scripts' call command (`script_bridge.c`) still `m_jsr` each handler at its
  own address -- dispatch through a C table as the event interpreter does
  (guard tested in C, `NATIVE_TIME_AS`, entries kept registered); the seven
  screen objects, the Black Omen watch, the vehicles' controller and the
  drift are pure C already, and only handlers that call the script
  interpreter mid-body need a guard-then-call-out helper (the next chunk);
  `screen_objects.c`'s handlers read the starting command's operands by
  position (decode once into named values), `moved()` reproduces byte
  wrapping for inputs the game never produces, and the display byte is
  rebuilt only to be decoded (also `display.c`); `epoch.c` leaves the
  `EPOCH_ARRIVALS` locations and rows unnamed, and `keep_a_stray_spot` should
  name the writer that resets the spot; `vehicles.c`'s `LOCATION_NUMBER`
  repeats `field_data.h`'s `LOCATION_NUMBER_BITS`, and the vehicles in the
  air's `parked_here` repeats the boarding's `dactyl_on_this_map` and
  `epoch_on_this_map`, the latter with the location compared whole, as the
  original's two tests do; `vehicles.c` writes a signed-word test out by hand;
  the equipment guard `menu_list_what_fits_can_be_made` works out "can use"
  itself, and `power` starts at the slot's first item only for a leftover;
  `menu_data.h`'s new slot accessors split a comment from its function;
  `music.c`'s `overworld_play_the_song_in_view` sets the request's fields one
  by one past `ask_for`; `map_scenes.c` sets the view to 256x224 itself
  because the platform has no screen-size call (a platform gap).

- **What the overworld on foot's review left.** Give the packed follower
  route and wall result a `Route` type; name the second-party-place test;
  share walker placement with the leader. Centralize the map size and define
  wall reads past its edge before moving storage. Explain `OVERWORLD_ASIDE`'s
  raw addresses, name story flags and event-cell values, avoid the duplicated
  ground test in `walk.c`, and name the sign script patch and `music.c`'s `3`.
  Share `EVENT_SCRIPTS`/`EVENT_SCRIPTS_BANK` between handlers and party code.
  Preserve the setup and upload-queue contracts described in
  REIMPLEMENTATION.md, "The overworld's teardown".

- **What the party exchange's review left** (merged with the exchange and
  the gate's song; none rated translation): the emptied place's dimming
  halves fifteen words past the palette as scaffolding, which the native
  build skips; that path runs every time the panel shows with an empty
  active row, so show the skip is invisible or keep the halving, and the
  TODO's "field's tables" is wrong (the words land on `battle_*` cells at
  `$7E:A5A2`-`A6A0`); the same "free" test is written two ways in
  `exchange.c`; the window lists and label strips are raw buffer and ROM
  addresses; `TWO = 2`; window edges packed `right<<8|left`; screen bits as
  `0x17`; the held cursor's shift written as a byte and read as a word; a
  mechanics comment above an enum; `menu_gate_song_wanted` writes cells
  under a question's name; `menu_wait_for_menu_music_playing_loop`'s name;
  `main_menu_bridge.c`'s `(void)option`; register the reserve slide's
  subroutines (`$C2:C85B`, `$C8BA`) once a movie reaches them.

- **What the main menu's, name entry's and description's review left**
  (merged with the menus' ninth chunk; none rated translation):
  `description.c` and `held_cursors.c` start
  their tasks by ROM address in logic (an enum, the address table in the
  bridge); `menu_start_typing` and the list slide take their inputs from
  cells their callers set (`naming.c` sets seven by hand) -- a description
  struct, and the slide's own parameters; positions kept as work-RAM
  addresses in `naming.c`, `description.c` and `lists.c`; `who` a bare byte;
  `names_given` ORs in the character index and its comment is wrong;
  `naming.c` draws the grid string by address with an unexplained `+ 1`, and
  `menu_naming_open` is a 57-line run of setters (the grid, the portrait,
  the label); `menu_naming_cursor_place` builds an x|y<<8 word only for the
  bridge's flags, `menu_name_cursor_before` is written plainly but read only
  by leaves, `menu_naming_give_the_name`'s bool feeds only a leave, and
  `description.h` exports `menu_leave_typing_started`; `naming_bridge.c` has
  a comment above the wrong function; `someone_has_it` run twice; the naming guard redefines the grid's constants;
  `shop_equipment.c` has a dead `any`, an unexplained `stale - 1`,
  scaffolding written every pass, and writes the status record by byte
  offset; `description.c`'s `item_row_fits` checks nothing.

- **What the party's services' review left** (merged with the services and
  the healing; none rated translation): `AYLA`, the record size `0x50` and
  `FIRST_ROCK`/`ROCKS` are defined in several files (the bridge re-decodes
  the rock test where `party.c` has `is_a_rock`); `party_fill_mp` masks MP
  with `& 0xFF00` (narrow the map); `MenuItemPlace.category` is a bare byte
  beside `MenuItemCategory`; `heal_member` reads the character from the cell `menu_load_member` set
  rather than taking it; `healing_bridge.c` tables call sites by an enum;
  `saves_power_on` writes `save_check_slot` every pass while passing the
  slot; `saves_start_a_new_game` writes "who has joined" as a word over
  `battle_party_placement_tile_x` with no aliasing in the map; the service
  table's `.wide` column is register width as data; one fuzz entry fills
  `7E2980-7E2982` twice.

- **What the settings screen's and the shop's review left** (merged with
  both; none rated translation): `shop.c` reads every item's power past the
  148-entry table, up to 108 bytes past, where the map's `unchecked` reason
  and TODO cover only the items screen's accessory byte -- a named function
  with a "no power" result, the reason updated; `give_button` mixes rows and
  assignments where the ROM's table at `$C2:C2BD` is the identity for 0-7
  (one index type); `swap_stray_places` wants a name for what the player
  sees; bit 0 of `story_party_rules` has three names in three files;
  `take_at_most` reads back the limit it just wrote.

- **What the save code's review left** (merged with bank `$FF`; none
  blocking): `SAVED_TECHS_UNLOCKED = 0x7E3` is a magic offset into a saved game (derive
  it from the run table); `menu_slot_game_at`
  still hands the checked slot to the summary's reading (pass the slot's
  place); the gate swirl's phase and frame count are positional indices into
  `menu_gate_screen_state`; `menu_put_character_colours` reads the colour
  table by byte offset; `count_on_the_cartridge`'s comment should say when
  the count of finishes moves on; a misplaced comment in
  `digits.c`; `leave_parts_noted` pulls `ram.h` into `techs.c`.

- **What the techs screen's review left** (merged with the techs screen; none
  blocking): `tech_screen.c` takes the thumb's direction from an unsigned
  carry (`step < 0`), `scroll == 0` as a signed test and `NOBODY` through
  bit 7; `WRAPS_TO_THE_LAST` reproduces a sign test for kinds the guard
  refuses (a plain wrap); `menu_techs_after_casting` returns the old MP only
  for the leave and subtracts a byte that can't borrow; `MenuTechCost.part`
  is read only in `ORIGINAL_EFFECT`; `techs.c`'s `TechWalk` is rebuilt every
  pass for scaffolding (once, after the loop); the cost is read back from
  `tech_cost_needed` right after `menu_tech_cost_for` returned it; the
  list's rows move by `wram_copy` on raw tilemap addresses;
  `menu_tech_effect_byte` takes a byte offset (`{factor, restores}`); the
  status record's `MP_AT` and `ACCESSORY_AT` want map fields;
  the bridge repeats constants `tech_screen.c` names, and `TECH_RECORD_BYTES`
  is in two files; `item_screen_bridge.c`'s `SITES[w]` runs a local enum
  beside `ItemUseKind`; the extra empty member rows' comment should say what
  the player sees; the members step's guard should check the list only when
  `menu_tech_list_shown` matches the member and kind.

- **What the items screen's review left** (merged with the item and
  equipment screens; none blocking): the sort reaches places 242-255 through `inventory_*_unchecked` where the map
  names them `menu_inventory_past_the_items` (reuse `items.c`'s split); item
  figures are read as `figures + FIGURES_*` and through cells `menu_item_place`
  just wrote -- an accessor returning the place's figures record;
  `raise_the_figure` adds a word across two record bytes for a figure the cap
  rules out (`+1`, and a guard refusing 255);
  `point_along_the_list` repeats `point_at_listed_item`'s tail;
  the slide leave repeats
  `slide_brings_a_row` with `0xF0`; `hide_the_scroll_bar_and_cursor` takes
  two cells down as one word with a borrow; `hide_the_scroll_bar`, the scroll bar's thumb at a row (`thumb_at_row`,
  also the techs screen's) and the equipped-slot layout `0x27` are copied
  between files; also the scroll bar's hidden flag
  set to `FIRST_MEMBER_PLACE`, `menu_leave_load(s, 0x62/0x42/0x2E)` literals,
  `& 0xFF00` merges, `hw_set_dividend` and the list-rows scratch in logic
  files, `menu_items_after_*` named by position, `put_bands_at_rest` taking
  byte offsets, and the cursor's item
  written to a cell and read back.

- **What the techs screen left** (`menu/tech_screen.c`; none blocking): the
  member step's guard wants the list made for the member and kind it
  shows (`menu_tech_list_shown`), which a screen just opened only has by
  chance, where choosing on that very frame reads the list as it stands (the
  original reads it too; the library's openings pass); the member list's and
  the tech list's slides and the techs' paging are reached only by fuzzing
  (no movie scrolls past three members or eight techs); two timing exceptions
  follow from the window lists' strings' spread (`menu_show_pointed_tech`,
  `menu_choose_tech`), which a calibration of the empty member row's list
  alone would take back.

- **What the field's screen, map and dialog review left** (none rated
  translation): `screen_setup.c` tables its fade-ins' ROM sites by kind,
  names `field_setting_1e` for its address (it keeps the screen dark), keeps
  a second copy of the layer maps' VRAM places (`display.c` has them), and
  runs a straight line of setters that belongs in one operation; `map.c`
  walks the exits by byte offset, and `field_packed_map` emulates a carry no
  map number reaches; `objects_start.c`'s `object_start_afresh` is 24 setters
  and writes `animation.c`'s own mode, one-off and half-passes storage, and
  its `clears[]` are RAM addresses spanning variables; `dialog_window.c`
  passes `PairSites` tag tables, has `SEND_ONCE` as `0x81` where `nmi.c` has
  `0x80`, a dead `TAKEN_ONCE`, a bare-byte `choosing`, `step_sets_a_glyph`
  re-running the window's growth, and handles a row past 127 the game never
  sets; `slots_for_kind[9..11]` uses bare kind numbers and `nmi.c`'s
  `run_screen_transition` has unnamed cases; the map's
  `field_window_staging` source names a function that doesn't exist;
  `FIELD_MESSAGES` belongs in `field_data.h`; the choice-active constants
  want an enum; a test should tie the `C0:0B38` hint to the disassembly.

- **What the field's frame and loading review left** (merged with power-on
  and the field's frame; none blocking): scaffolding written through
  accessors named for another use (`battle_start.c` and `battle_return.c`
  write the screen corner through `set_field_own_tile_x/y`, the enemy's kind
  through `set_field_direction`) -- name the second use in the map or use
  `original_cell8`; `location_description_at` takes a byte offset (take the
  location, return the record);
  `screen_corner` is in `battle_start.c` and `battle_return.c`; BG3's tiles at VRAM
  `0x5000` and the bank `$7F` buffers (`0x7F5080`, `0x7F6000`, `0x7F7000`)
  are literals, and `main_loop.c`'s `power_on_clears` uses raw ranges;
  `location.c` tests `field_map_has_no_bg3_set(g) == 0` where a bool query
  wants to sit beside `field_map_names_a_bg3_set`; `palettes.c`'s
  `SlotLooked` shapes only scaffolding, and `slot << 1`/`>> 1` is an
  unnamed palette encoding; `field_kept_party_script_tiles` packs `y<<8|x` by
  hand; `program_install_*_handler`
  takes a ROM address where a later target wants a handler identity.
  `field_walk_the_leader`'s vblank-tail timing exception (11.6% of calls) has
  a real cause, the step that recentres the screen: give that path its own
  `charge_as` in `walk.c` and drop the exception.
- **Shadow mode can't see a held-over native routine.** The field's main
  loop once stayed suspended across every frame's end, and later stacked
  eight deep when the original's location change reset the stack under it;
  shadow mode passed both, and only the long routes found them, as a hang
  with no message. `ct_dev` and `ct_drive` should warn when `native_suspended`
  stays above 0 for many frames or an entry nests inside itself.

- **What the screen effects' review left** (merged with field's
  screen-effects chunk; none blocking): `draw_circle` and
  `draw_the_searchlight` hand `scene_circle_draw`/`field_draw_window_shape`
  their inputs through cells -- pass them as parameters and keep the cells as
  scaffolding; the bytes of `field_line_bg_maps` and
  `field_line_wave_bg2` want fields in re/ram.toml (and `BG2_WAVES` a
  meaning); `as_the_dispatch_has_it` sets A/X/C/V on every `$FF` path where
  only the restart and `$9F` paths need it (only V-clear matters on the C
  path); the `RIPPLE_PLAYED` comment says the script clears it (nothing
  does); `CircleKind.mode` untyped, bare flag bytes for layer work,
  `send_bg2_halves`' `<< 1`, designated initializers for `screen_commands`,
  `SEARCHLIGHT_LENGTH` repeated; a misplaced comment in requests.c.

- **What the event language's review left** (merged at 136104d; none
  blocking): the palette animation kinds are defined twice (`engine.c`'s
  `*_FORM`, `core/palette.c`'s) and `ColourEffect.animation` carries the raw
  byte -- one kind enum in `core/palette.h`, typed fields for the kind and
  fade channels; `cast.c`/`cast.h` say
  the first three NPCs note their graphics where every NPC from the third
  overwrites the third record (`$C0:463D`);
  `unsigned place` where `PartyPlace` fits; `goes_on_after_sending` and
  `goes_on_after_leaving` near-twins; `colours.c` mixes `|` and `+` for the
  palette and repeats an `if`; `items.c`'s script-menu-shown bit wants its own
  read-and-clear. The loose cast fuzz variants (`$82`, `$83`) mismatch when a
  random unpack place runs past `$7F:FFFF`, which the game's sets never reach
  (characters.c, `event_cmd_cast_enemy`): accepted, and kept to the bank's
  upper half in the variants that must be clean.

## Generator

- **What the dc-evidence tooling doesn't check yet** (PORTING.md,
  "Declarations liveness can't prove"; the audit of `tools/v_readers.py`):
  - a registry entry's `.dont_care` is held only to the trusting solve (the
    work-outs' `DC_V` among them): hold it to the strict one, with the same
    notes allowed (and see "Stops and entries don't get the strict solve yet"
    below);
  - the library run masks the whole stack page (`$7E:0600-06FF`) where only
    the bytes the scrambled registers were pushed to need it: mask those;
  - a bounded table's targets are matched to the analysis's contexts by
    address alone, not by the widths the dispatch enters them with;
  - an interrupt handler reads the interrupted code's P from the stack, which
    neither `tools/v_readers.py` nor the run looks for;
  - `tools/disasm/high_byte.py` takes a `TDC` to clear A's high byte, which
    rests on D's high byte being zero there (the battle's direct page
    `$0000`); following D itself through the program (it is unknown at every
    vector, and `PHD`/`PLD` only pair within a routine) would make that static
    too. `ct_dev --index-check` shows it over the library meanwhile.
  - The enemies' condition dispatches after the first (`$C1:8769`,
    `$C1:B021`) have no `index_reach`: A's high byte there is whatever the
    dispatch above them returns with, and 44 of the routines its 128 words
    reach return with it unknown (`$C1:925D` ... `$C1:AA25`;
    `tools/v_readers.py`'s `high_byte_findings` lists them). The evidence
    doesn't need them today; a later note that does will want those routines'
    returns shown to clear it, or a guard at the dispatch.

- **Stops and entries don't get the strict solve yet.** The don't-care check
  holds call-outs to liveness that trusts no target set the machine picks at
  run time (REIMPLEMENTATION.md, "Call-outs into targets the machine picks"), but not a
  registry entry's set after its return or a stop's. Held to it, 132 would be
  findings: 108 menu entries (the menus' `JMP (abs)` through RAM, `$C2:0553`,
  `$C2:1819`, `$C2:1836`, `$C2:8203`, `$C2:828E`, `$C2:EFD0`, in the callers
  they return to), 15 scene entries and 9 scene stops (the task lists at
  `$7E:39CF`/`$7E:3998` and `$00F0`'s dispatch after the return). Unlike the
  call-outs, there the dispatch runs on the game's own state after the port
  has finished, so the trusting solve is sound for any state the game makes;
  what it isn't sound for is a port that leaves a task list or pointer the
  original wouldn't, which shadow mode compares in memory anyway. Deciding
  whether to fuzz them with the rule (and scrambling the entry's own set at
  its return, as `--poison-dc` does at call-outs) is the open part.
- **Code the program writes over itself.** A `[[ramcode]]` section with `over`
  or `patch` has an image per alternative, and liveness reads the patched
  one. A call-out into such a section is checked against that image only; one
  whose callee differs between the images needs both summaries joined, or the
  call counted as dynamic.
- **Duplicate event command rows.** The event language table silently lets
  the last row for an opcode win. Reject duplicates when building the table
  (`game/event/engine.c`).

- **Reads of scaffolding the effects check holds in its baseline**
  (`tools/effects_baseline.txt`; REIMPLEMENTATION.md, "The tooling pass after Phase 3's
  first chunks"). Each is a value the native build reads stale unless shown
  otherwise; answering one deletes its line. What the library's shipping-mode
  runs catch (`ct_dev --native on --scaffold-reads`), by subsystem:
  - menus: `menu_scratch` (`$0001`), read by `menu_draw_member_portrait`
    where its own scaffolding wrote it last.
  The source check's findings on main are broader (battle 87, field 29,
  event 3, menu 103, scene 15, core 1, by element, after the battle's and the
  menus' first chunks; `python3 tools/effects_check.py`): most
  are the entry wrappers reading the operands the original's callers hand
  them, which a line with that reason answers, but the `[only]` ones in logic
  (`python3 tools/effects_check.py | grep only`) are where to look first.
- **A debt waits a frame at most for a handler taken at its boundary**
  (`runtime/port/native.c`, `DEBT_HANDLER_CLOCKS`): a handler that moves the
  stack and doesn't come back within a frame, but does later, gets the rest of
  the port's time paid inside it again. None does in the library; a debt that
  followed the handler's own return (its RTI to the boundary's PC) rather than
  a time bound would be exact.
- **Nested comparisons end at the enclosing routine's point.** An entry the
  original reaches inside another port's stretch (the menus' labels inside
  `menu_open`) is compared up to its first point, and set aside (`abandoned`)
  when the enclosing routine's point comes first. Following both past it
  needs shadow mode to check every active comparison at each step rather than
  the innermost: the enclosing one resumed by its callee's return while the
  nested one waits for a later instruction.
- **Why a guard refused a real call isn't reported.** Shadow mode and the
  calls log count refusals but don't say which condition refused or what the
  state was; agents have added a print and rebuilt to find out (battle's
  phantom timer slot, the menus' refusals). A `ct_dev --guard-details N` that
  names the entry, the failing condition and the cells it read, for the first
  N refusals, would save that; so would counting refusals per entry in the
  shadow report.
- **`fuzz_compare.py` doesn't scramble.** A rewrite's before-and-after
  comparison runs without `--poison-dc`, so a declaration the rewrite widened
  shows only if the port also stopped passing the original's value. Passing the
  flag to both builds needs the predecessor to have it too; until every
  predecessor does, run `ct_fuzz --poison-dc` on the rewrite alone.
- **"No game state produces it", checked by a tool.** A proposed guard
  needs a count of real calls it would refuse, with entry and state details,
  over the library. A reusable guard-probe tool would replace temporary
  prints and rebuilds. The scene review's proposed multiply carry guard
  would have rejected real calls; syntax alone cannot justify a domain.

- **M4**: skip flags nobody reads, call generated blocks directly where it's
  safe. Speed only; rendering and sound dominate now.
- **The draft tool's gaps** (REIMPLEMENTATION.md, "What drafts don't do yet"): D and DB
  for code only interrupts reach, and the field's main loop after a battle,
  which the frame analysis gets wrong (a guard catches it: 432877 calls of
  `field_scroll_layers`); operand names for the battle script handlers;
  turning I/O register writes and the multiplier into platform calls and C
  arithmetic; stub signatures that still vary by what a call reads back.
- **Drafts and ports don't use the new declarations yet** (REIMPLEMENTATION.md, "Between
  the waves: the validator"). The draft tool writes A's low byte back wherever
  its high byte is live and gives a stop the entry's set; `DC_AL` and
  `m_stop_at_own` would let it name exactly what is dead. Among the ports,
  `python3 tools/dontcare_check.py --list` has 300 declarations that could add
  `DC_AL` (`could add`), and entries whose sets a stop kept small
  (`battle_update_combatant`, `event_run_object_script`, `menu_run_objects`,
  `field_object_in_the_way`, the event dialog and condition commands).
- **Two answers to "what is dead after this returns", and only one is
  authoritative.** `disasm/out/liveness.json::dead_after_return` includes
  escaped returns through a caller's frame; `tools/generate/show.py`'s
  `ret_live_mask` asks at the entry's contexts, falling back when unknown.
  For `$C2:0568` they yielded `X N Z` and `A X Y N V Z C` respectively,
  with the gate accepting the latter. Move the authoritative query into
  `liveness.py`, emit both explicitly named answers and document their scopes.

- **What the overworld on foot left** (history/reimplementation-chunks.md, "Phase 3, overworld: the
  world map on foot"):
  - the walkers' event steps run their scripts through the interpreter's
    bridge (`menu_run_object_script_from`, `menu/script_call.h`), which, as
    the interpreter's own entry does, hands a command it doesn't run (an
    opcode past the language, a cell outside low RAM, a call of the
    original's code that doesn't come back as it went) to the original's
    interpreter, which returns into the walker's code after its call. No
    game script does that; a random one can (a map copy over the stack), so
    their fuzzing gives them scripts of the game's commands. No movie steps
    onto an event, so the steps are fuzzed only;
  - the halt (`$C2:250D`) is a pass at a time that comes back to itself,
    as the original spins: a host that wants the game to end there (a
    message, a reset) has nothing to tell it so yet. No map's data has an
    open halt as far as the library goes;
  - the follower's search for walls doesn't keep its legs on the map: a leg
    from the map's first row looks at the row before it, which the original
    reads past the map's end (`overworld_map`'s `unchecked` pair,
    `terrain.c`). The native storage has to keep the bytes there, or the
    search keep to the map;
  - the main loop's arms were compared by shadow mode alone (fuzzing can't
    go past the frame's wait), and since the overworld retired the gate
    (REIMPLEMENTATION.md, "The overworld's teardown") by nothing but the
    recorded outcomes. No movie takes an exit to an event
    (`$C2:2417`-`$C2:244F`) or opens the party's exchange from the map
    (mode 9, the `menus-end-of-time` exchange is the gate's); nor walks
    from one region into another (`$C2:3011`-`$C2:309D`, the music's fade
    and the new song), nor clears the sign from the menus (`$C2:5711`): a
    movie that does would protect them.

- **What the vehicles and the screen's objects left** (history/reimplementation-chunks.md, "Phase 3,
  menus and overworld: the vehicles, the screen's objects and the calls made
  where they go"):
  - the objects' loop (`menu/objects.c`) and the scripts' call command
    (`menu/script_bridge.c`) still call a handler or a named routine through
    the machine, at its own address, so that each keeps its shadow
    comparison and its calibrated time: the native program needs a C table of
    the handlers and of the routines the scripts name, each row the
    operation's own (`NATIVE_TIME_AS`), with what nothing ports handed to the
    original;
  - no movie flies the Epoch over the Black Omen or enters it
    (`$C2:4510`-`$C2:452F`, `$C2:4A0A`-`$C2:4A19`), takes off at the story's
    point `$D3` or boards at `$D2`, or sets off for `$1F2` before the Epoch
    has its wings (the stray spot it keeps there, `keep_a_stray_spot`): those
    paths are fuzzed only;
  - the Epoch's second sprite is fuzzed on few states (its guard asks every
    animation it may be given to be one the language reads, which random
    sprite scripts rarely are): a variant with the slot's scripts kept would
    cover it better.

- **What the menus' tenth chunk left** (history/reimplementation-chunks.md, "Phase 3, menus: the
  party's exchange"):
  - the main menu's exchange (the active party alone) takes the reserve's
    count as the screen before it left it: with a count, a push across
    takes the cursor round the reserve's empty rows, where the original
    searches for ever and the game hangs; the port stops after 64 steps
    with nobody under the cursor (`exchange.c`, `MOST_STEPS`). Whether a
    game state gets there (a count left by the settings', the techs' or the
    gate's screen) is open; the library's never has one;
  - the reserve's slide (`$C2:C85B`, `$C2:C8BA`, `$C2:C949`) and its scroll
    bar want more than four members in the reserve, which a party of seven
    characters never has: no movie slides, and fuzzing compares a slide up
    to its first frame's wait (the rest is past the menus' task switch);
  - the panel (`$C2:C9E5`), the gate's opening (`$C2:C7AC`) and its song's
    change back (`$C2:CC0E`) span frames, so shadow mode compares them only
    inside the calls that enclose them (`menus-shop-equip` opens the gate's
    screen in Medina, where its song changes; `menus-end-of-time` at the
    End of Time, where the song is the gate's already); fuzzing compares
    them up to their first call out;
  - an emptied place's byte is taken for a character by the dimming of who
    stays, which halves fifteen words past the palette
    (`sprites.c`, `menu_leave_colours_dimmed_past_the_palette`): scaffolding, so the native build
    doesn't, on the field's tables the menus lay their own over; show that
    the field lays those out again as the menus close, or keep the halving;
  - `menu_field_song_volume` (`$7E:04FB`, the volume the place's song fades
    back in to) has no writer the disassembly reaches by its address: find
    it, or drop the cell's name for what writes there.

- **What the menus' ninth chunk left** (history/reimplementation-chunks.md, "Phase 3, menus: the
  main menu, the save screen's opening, name entry, the description, the
  list slide and the shop's equipping"):
  - the save point's save screen's opening and choosing (`$C2:D4F8`,
    `$C2:D506`) are reached by no movie's reference play (the library saves
    from the main menu's panel), only by the shipping mode's drift, where
    `endings-beyond-time`, `late-lavos` and `late-omen-zeal` end up on one
    and sit there (59,000 frames of it): fuzzing is their check, and their
    `cost_milli` is the placeholder. The frame among the slots they share
    with the main menu's save screen (`$C2:CFA2`, `menu_move_among_the_slots`)
    is compared in the reference play through the main menu's;
  - the naming's and the main menu's openings and the shop's member opening
    span frames on every call, so shadow mode compares them only inside
    their callers and the timing gate can't measure them (`cost_milli`
    5000); the name entry's three steps are at the floor, their windows
    charged at the window lists' rate (three drift exceptions);
  - the description keeps the typing's spare cells (`_pad`) and the upload
    record's eighth byte as the original's, as scaffolding; a native
    storage drops them.
- **What the menus' eighth chunk left** (history/reimplementation-chunks.md, "Phase 3, menus: the
  party's services, its figures and its healing"):
  - the figures and the healing leave the multiplier's and the divider's
    results (`hw_set_multiplicand`, `hw_divide_by`) and the healing's
    numbers' cells as the original's work did, as scaffolding: they go with
    the gate, and the figures then need no hardware arithmetic at all;
  - the healing's rising numbers (`$C2:9016`, `$C2:9047`) and its showing
    (`$C2:8F55`) span frames, so shadow mode compares them inside the call
    that encloses them, and fuzzing up to their first wait for a frame;
  - the services, event's party commands and a New Game+'s load are guarded
    to an active party of characters (`party_record_shows_characters`): a
    record holding something else would index past the characters' records,
    as the original would. A native storage makes the record a type that
    can't.
- **What the menus' seventh chunk left** (history/reimplementation-chunks.md, "Phase 3, menus: the
  settings screen and the shop"):
  - the settings screen's eighth button assignment (the shoulder buttons'
    row) reads its list of rivals past the table's end, into the code after
    it, and the game swaps the places that list names as it does the other
    assignments': the boxes' first two cells and cells past them, reached
    only by giving confirm or cancel the L button. `swap_stray_places`
    reproduces it through `menu_settings_buttons_unchecked`; a native
    storage has to decide whether the settings keep it;
  - the library buys and sells at two shops (`menus-shop-inn`,
    `menus-shop-equip`) without paging or sliding the goods' list; fuzzing
    is the check for the slide, the quantity's limits and a full purse, and
    a movie through a big shop's list would add shadow mode's.
- **What the menus' sixth chunk left** (history/reimplementation-chunks.md, "Phase 3, menus: the
  items screen and the equipment screen's windows"):
  - the window of who can equip an item reads an accessory's power past the
    table the menus build (`menu_item_powers`, 148 entries: the weapons',
    the armour's and the helmets'), as the original does, through the
    variable's unchecked accessor: whatever work RAM holds there shows beside
    the accessory's (empty) label. A native storage has to decide what the
    window shows for an accessory;
  - the library never slides the list of the party's items, shows who can
    equip one, or uses a tab, an item by chance or the party's rest, and the
    member window's loop is checked past its first frame only on the one
    tonic `menus-field-menu` uses: fuzzing compares each up to its first
    wait for a frame. A library movie that does them (from a save with tabs
    and a Lapis, more than ten places of items) would keep them checked.
- **What the menus' fifth chunk left** (history/reimplementation-chunks.md, "Phase 3, menus: the
  equipment screen and its member list"):
  - the library never slides the member list, switches members with the
    shoulder assignments, slides the list of what fits or refuses a slot, and
    the main menu's panel is interrupted on its one call in each movie that
    opens the menu: a scratch movie from `menus-field-menu`'s save checked them
    (REIMPLEMENTATION.md), and fuzzing reaches each only up to its first wait for a
    frame. A library movie that does the same (the equipment screen from a
    save with more than three members to choose among, an accessory list of
    more than nine) would keep them checked;
  - the member list's colouring of one row (`$C2:9EB0`) reads where the row
    is from a cell its two callers write before the call; both are ports now
    that colour the row as C, so the entry is reached only by fuzzing.
- **What the menus' fourth chunk left** (history/reimplementation-chunks.md, "Phase 3, menus: what
  the menus' ports called most"):
  - the menus' vblank work (`$C2:842A`) calls the per-frame input as a
    call-out (`m_jsr_dc`), not as C: run through `menu_run` it changed what
    the vblank work's fuzzing could compare (415 of 500 to none), which
    wants looking into;
  - a count of 0 tiles or rows in the recolouring helpers counts the whole
    bank, as the original's 16-bit loop does; the guards turn those away,
    and nothing in the library asks for one.
  - from the chunk's review: `RUN_OUT` in `menu/sprites.c` stands for three
    things (a sprite's absence, a hidden record, a frame's time run out);
    the records' task and its guard each work out whether an animation moves
    on, where one pure `animation_due()` would say it once (the countdown
    its own statement), with the `0x0F` sprite count commented and the
    orphaned comment above it placed; the portrait's accessors take a slot where they could take the character,
    with the guard refusing a character past the eighth; the menus' tilemap
    helpers want one header (`menu/tiles.h` beside `menu/windows.c`'s) and
    one `menu_note_palette_changed`; and the smaller notes on `cursor.c`,
    `sprites.c` and `tiles.c`.
  - the `bucket_lavos` route's last fight (Lavos's core) took 311 tries and
    about 2.5 hours in the chunk's `--long-routes` run, ending at frame
    211,229, where two earlier runs won every fight at the first try and
    ended by frame 154,652; its first fight also ended far later (frame
    113,838 against 54,968 and 64,472). Shadow and trace identity were
    clean, so it is likely timing reaching a harder battle state; whether
    the menus' per-frame input (which runs in every mode) or the merged
    main moved it is not worked out. Part of the time is the fights'
    restores: each (a wipe's retry, a command menu's checkpoint) replays
    from the last base, which a native battle, suspended for its whole
    length, keeps before the fight (MOVIES.md, "resume"); a base ct_drive
    could take inside a battle, or fewer restores in `campaign.fight`,
    would bound it.
- **What the menus' third chunk left** (history/reimplementation-chunks.md, "Phase 3, menus: the
  characters in the records"):
  - six-block sprites are left to the original by the guards (their sprites'
    offsets run past the record into the next); no menu shows one;
  - what sets `menu_record_buffer_half` (`$7E:0D72`: nothing in bank `$C2`
    writes it) is not worked out;
  - `$C2:F6F5`, an entry into the animation's step past its frame's advance
    that nothing calls, is not ported;
  - the sprite tables' accessors in `menu_data.h` take byte places the port
    works out (`a.graphics * 3 + table`); accessors that take the sprite's
    numbers would keep the tables' layout in one place (review item 64), and
    the long and short assignments' test is written twice (the place and the
    size);
  - `sprites.c` steps its pointers with `in_bank` in some places and `+` in
    others, as the original's word and long adds do; a comment where each
    is chosen would say why;
  - the palette's changed count is bumped in place
    (`set_menu_palette_changed(g, menu_palette_changed(g) + 1)`); a
    `menu_note_palette_changed` would name it;
  - `menu_record_animation_count` is a facing's frames, which its name
    doesn't say;
  - the save screen's chapter label builds its place and calls the label
    setting inline in its entry and its guard; a helper would say it once;
- **What the menus' second chunk left** (history/reimplementation-chunks.md, "Phase 3, menus: the
  typesetter, the graphics and the items"):
  - the typesetter's `$12` followed by 2-7, which jumps into code in the
    original that isn't a handler, ends the call in the port as a pause does,
    setting nothing: no string has it (6 or 7 of 20,000 fuzzed states read
    random ROM as text and reach it);
  - the typesetter's control codes as a C table with a column for where a
    name comes from, in place of `control_code`'s chain;
  - the glyphs by index (a glyph number and its width's index are the same
    thing counted from `$A0`), rather than by the string's byte;
  - `items.c`'s tables reached through record accessors, and the story's and
    the place's notes (`menu_story_items`, `menu_place_items`) named for what
    they say;
  - `graphics.c`'s tile sets named by what they hold rather than where they
    go;
  - the save screen's `menu_scratch_save_member_*` fields renamed for what
    they hold;
  - what the three bytes at `$7E:2C53` the menus copy as they open are, and
    the story's bits the item lists read (`menu_story_items`,
    `menu_place_items`), is not worked out.
- **What the save code's chunk left** (history/reimplementation-chunks.md, "Phase 3, menus: the save
  code and the rest of bank `$FF`"):
  - `$FF:FF3E`-`$FFF4`, a menu record's routine with a jump table at
    `$FF:FFE2`, behind a return the analysis finds nothing reaching, is not
    ported (bank `$FF`'s 10% left);
  - a New Game+'s load (`menu_load_carried_over`) spans frames, so its one
    call in `menus-new-game-plus` is interrupted; no movie reaches a
    character's sprite colours (`menu_put_character_colours`); and the number
    tiles' and the gate's drawing are interrupted on their only calls:
    fuzzing is their check;
  - what the cartridge's count at save memory's `$1FE1`
    (`SAVES_TIMES_FINISHED`) counts: the save moves it on when the game's
    `times_finished` equals it, which doesn't read as a count of finishes.
- **What the menus' first Phase 3 chunk left** (history/reimplementation-chunks.md, "Phase 3, menus"):
  - `menu_erase_slot` (`$C2:E8B7`) is fuzzed only up to its wait for the next
    frame (`ct_fuzz --stop-at-call C2821E`, 1,875 of 2,000 states compared,
    none mismatching). `menus-new-game-plus` erases a slot, but the wait is a
    task switch shadow mode can't follow, so it sets the erase and the
    answer's yes that runs it aside there (`abandoned`); the answer's no is
    compared. Comparing a call across the menus' task switch would check the
    rest;
  - the label entries (`$C2:D156`, `$D19F`, `$D266`, `$D28D`, `$D296`,
    `$D2C4`) only run inside `menu_open`'s stretch, so shadow mode compares
    them up to their first point and sets them aside there (`abandoned`); the
    timing of that part calibrates them, what they do after it is in
    `menu_open`'s own rate (28004, against about 5000 for the menus' other
    bridges; REIMPLEMENTATION.md, "The tooling pass after Phase 3's first chunks");
    `$C2:D306` (a label moved two pixels right) is inline in `$C2:D2C4` and no
    entry;
  - a window list's strings that include work RAM (a name, a number) draw
    through the text port's accepted divergences when fuzzing fills that RAM
    at random (7 of 2,000 fuzzed `menu_window_draw_string` states, 2 and 1 of
    the lists'); no list of the game's reaches them.
- **What chunk 1's review of the menus left for later** (history/reimplementation-chunks.md, "Phase 3,
  menus"): `save_screen.c` renamed for what it holds (the slots' summaries);
  `menu_save_slot_in_use`'s fourth entry, the fourth row, split out in the map
  with named values, and the load screen's steps by name rather than
  `step ± 1`; the inside's pattern as `pattern[row % 4][column % 4]` with the
  block moves' time charged in scaffolding; windows.c's operands decoded once
  into a struct per operation; one `first_push()` in cursor.c; names for
  `MENU_SOUND_6` and `_7`; a `SlotSummary` record for the summary's byte
  offsets and the literal `$9890`/`$98C9`; opening.c's driver calls as a table
  of `PF_CALL_DC` sites;
  - from the fixes' re-review: after a full `WINDOW` command and after
    `menu_window_in_design` (`$C2:ECDB`) the inside's columns and rows are
    still written only as scaffolding, so an inside drawn after one of them
    would read stale sizes in the native shape (no screen does today);
    `windows.c`'s "what the original leaves" comment still says nothing reads
    those cells; `tools/disasm/keeps.py` steps over calls as reading nothing,
    so a callee on the scheduler's path that reads a register would be missed
    (the path has none today); `read_cursor`'s `(void)r;`.
- **Liveness across a saved stack pointer.** The overflow flag is still live
  after `$FD:C2C1` and the rest of the field loop's callees (the carry and X
  no longer are): the menus' entry `$C2:800E` saves P and then copies S to
  `$0920` (`TSX`) to run on a stack of its own, which the pairing of pushes
  can't follow, so its `PHP` reads every flag for every caller. The same holds
  for the scene program's task lists (`$7E:3994`, `$7E:39C8`), whose tasks
  leave through `JMP $39A7` into the loop's own code and carry on with the
  next task rather than return.
  Following S through the cell it is saved in, and a callee that finishes its
  caller's iteration, would free them (REIMPLEMENTATION.md, "The tooling pass after
  wave 1: the analysis"). The menus' task switch (`$C2:821E`, `$C2:822B`) is
  a `[[keeps]]` hint instead (history/reimplementation-chunks.md, "Phase 3, menus"); the scene's task
  lists could take the same.
- **Scene cells the menu objects use.** The C6 circle setup and motion routines
  (`menu/line_effects.c`, `menu/object_commands.c`) run on the menu's direct page
  and use the circle drawer's and the scaler's cells, which the map files under the scene; their
  drafts now give those cells scratch names (REIMPLEMENTATION.md, "The tooling pass
  after wave 1: the analysis"). Filing them where both subsystems see them (`core`, or
  entries on the menu's page) would name them again.
- **Gaps in the vblank check** (REIMPLEMENTATION.md, "Where video writes land"), which
  the suite runs with seven findings accepted (REIMPLEMENTATION.md, "The timing gate"): a
  hand port's region ends at its last service call, so the reference's work in
  code the original jumps to past it is `unmatched` (the menu loads);
  following the call stack in the reference would close that. And it names the
  routine whose writes land late, not the port whose time made them late.
- **Player without the interpreter**: the strict build's CI check is new and
  hasn't run on a real miss; the misses-feedback loop is documented but only
  exercised by hand so far (PLAYING.md, "Without the interpreter").

## Matching disassembly

- **The `matching` target fails its closure check C2.** The scene task
  dispatch at `$7E:3998` includes `$7E:F820` from `seqscript.tasks_0920`
  (`re/hints.toml`), outside every RAM-code section. Identify the script
  operand as code needing a section or as data; the missing return currently
  forces assumed widths at `$7E:399B`. Epoch-dive coverage also names
  `$7E:3662`, outside the static reach of the patched `$7E:3634` loop; model
  that alternative image. These findings are retained pending a matching
  build, not inferred fixed from the scene gate's retirement.

- **MD2 leftovers** (MATCHING.md, "MD2 as built"): `$F6:24B1-7FFF` and other
  gaps (42590 census-unknown bytes), plus 228.3 KiB still typed `mixed`.
  The D0 graphics pool, CF 2bpp graphics, E4 sprite records and E5 exit lists
  now have codecs; tile-descriptor ownership and the E4 battle-record count
  still need stronger bounds. The strings' leftovers (MATCHING.md, "The string
  encodings"): the dialog tables at `$FF:D3FE` and `$CC:3A09`, whose extents
  aren't settled, and the menu strings passed to `$C2:EF65` by address in code.
  The event scripts' leftovers (MATCHING.md, "The event scripts"): commands
  and operands named mechanically (`party_routine_6`), and the 39
  command layouts read from handlers no port has checked yet.
  The sound driver's tables (found by the $C7 pilot, `core/sound_data.h`):
  `$C7:1871`, the songs' percussion (`sound_song_percussion_table`), is typed
  as one song's twelve three-byte entries, but the driver indexes 83 songs'
  worth (36 bytes each, to `$C7:241C`), and the two bytes per song at
  `$C7:241D-24C2` are still `mixed`.
  The battle scripts' leftovers (MATCHING.md, "The battle script formats"):
  names for the commands, conditions and actions that keep their numbers, and
  deriving the action and target tables (`$C1:B85F`, `B88D`, `B8BB`) from the
  parse. Not started: re-encoding for `lz` (today's check is that the
  decompressor consumes the bytes exactly; the event scripts, all `lz` streams,
  round-trip on their decompressed output and would be assembled from their
  decoded form with it: MATCHING.md, "The event scripts").
- **The RAM map's leftovers** (DISASSEMBLY.md, "The RAM map"): 43 addresses
  the code touches without anything establishing what they mean (menu 20,
  battle 11, core 9, scene 3;
  `python3 tools/ram_map.py coverage --open`), named as Phase 3 reaches each
  subsystem. The menus' three at `$7E:0D5D`, `$0D84` and `$0DC5` are the save
  slot summary's (`menu/save_screen.c`), and lie inside the object slots' span
  without being fields of a slot. Three more bits the object scripts read,
  `$7E:1BA7` bit 4, `$1BAA` bit 5 and `$1BAF` bit 1, are the story's flags as
  the map screens copy them in (`menu_story_flags`, fields `top_layer_*`):
  each holds the layer over the map still while the map scrolls, and nothing
  says what else they mean.
- **MD3**: every pointer symbolic, plus the static and dynamic shift tests.
- **MD4 residue**: 84 jump tables are bounded by layout alone, the claim that
  menu modes 8-10 and 15 are never selected is assumed, and C4 (RAM code) rests
  on the enumerated sections plus the miss report.
- **asar cross-check** of the generated source has never run (asar isn't
  installed here); the syntax points it would settle are listed in MATCHING.md.

## Platform

- **The older video forms can leave the service** (PLATFORM.md, "The services'
  gaps the reviews found, closed"). No port hands the SNES backend its write
  order at the call any more (the lint's `WRITE_ORDER` is empty): every site's
  sequence is in `runtime/platform/snes/encodings.c`. The older forms
  (`vid_upload_*`, `vid_fill_vram`, `vid_begin_vram_uploads`,
  `vid_set_line_effect`, `vid_snes_transfer`) and `VidSnesXfer` can go once
  nothing else calls them.
  The field's own screen setup (`$FD:C0D7`) sets part of a screen, which
  `VidScreen` doesn't express; it is a run of setters with values, which is
  enough until a partial screen is wanted elsewhere.

- **Video escapes after the retired subsystems.** The ordinary service calls
  have host implementations; `ct_native` renders their retained frame directly
  (PLATFORM.md, "Video from service calls, as built"). The remaining work is
  in battle and menus' remaining transfer descriptors, and in the scene's
  follow-up work; their teardowns did not remove these escapes.
  Do not replace their machine contracts by a narrower call before checking
  the descriptor's full domain and the state left for callers. The 23
  `machine_joints.py` occurrences are the 22 calls below plus a comment in
  `scene/screen.c`; none is in field, events, overworld, warp or core.

  | File | Calls/sites | What the service needs |
  |---|---|---|
  | `battle/uploads.c` | `$CD:09CE`, `$CF:E33B` (2) | Preserve the palette sender's data-bank behavior and stray RAM writes while the legacy machine form needs them; decode the queued transfer's destination/mode into a typed upload, with the full guard retained. |
  | `menu/display.c` | `$C2:9526`, `$C2:954B` (2) | Memory clear/copy, including source-bank and work-port wrapping, cursor state and transfer time. The field's `wram_clear` is the precedent, but these use the work RAM port, not the multiplier. |
  | `menu/script.c` | `script_byte` register writer and its `vid_snes_takes_register` guard (2) | Decode script object-configuration commands to `VidObjConfig`; keep refusal of every other register address. |
  | `scene/race.c` | `$7E:3D41`, `$7E:876E`, `$7E:4671` (3) | VRAM read-back with its initial read/step; a continued low-byte plane upload; a low-byte column upload with stride. |
  | `scene/rising_palace.c` | `$7E:672C`, `$7E:674D` (2) | Plain memory clear retaining the matrix state; VRAM map read-back with the original initial read/step. |
  | `scene/screen.c` | `$7E:30B6`, `$7E:30D2`/`$7E:30E6`, `$7E:3850` (3) | Two work-buffer clears and a sprite-buffer fill with the multiplier's value; separate memory work from the matrix values that remain video state. |
  | `scene/screen.c` | `$7E:38EA`, `$7E:38E3` (2) | Typed transfers decoded from scene descriptors, including their byte/word patterns and zero-length convention. |
  | `scene/screen.c` | `scene_clear_buffer`, `scene_blank_past_the_plane` (2) | Memory clears at `$7E:3C22`, `$7E:3C3E`, `$7E:3E71`, `$7E:7B98`; continued low-byte fills at `$7E:5476`, `$7E:85B2`. |
  | `scene/title.c` | `$7E:59DD`, `$7E:5AEB` (2) | Name the first fill's source explicitly instead of continuing an old DMA channel; continued low-byte plane fill for the second. |
  | `scene/staff_roll.c` | `$7E:90BD`, caller's `$7E:8DA9`/`$7E:8F77` (2) | Low-byte tile fill and high-byte priority fill with explicit start, stride and repeated value. |

  The native compatibility file models memory fills and copies and some
  data-described VRAM transfers, and counts unsupported escapes. Its existing
  behavior is not a replacement for those contracts. Add targeted service-fed
  pixel checks for each teardown. The register decoder remains the default
  `ct_dev --px-check` source; `--px-check-services` selects the direct source.
- **`video_test` checks snapshots, not pixels, for retained changes.** Its
  retained tile and sprite cases (`tests/port/video_test.c`) assert on the
  observer's snapshots; add assertions on the rendered pixels, so a renderer
  that ignores a retained change fails without a movie.
- **Native uploads at the line boundary.** The host frame model currently
  folds both handlers' uploads into the whole picture. A trial before the
  calibrated native timing merge captured lines above the line handler before
  running it: 14 sampled movies' recorded native pictures changed and the
  matching `field-floor-switch` checkpoint at frame 3000 was lost.
  Reconcile animated tiles and switch redraws with
  the frame model before changing that order; do not merely re-record those
  exceptions. The service observer already retains per-span memory when its
  caller captures at an actual boundary, as the service-fed pixel check does.
- **Mode 2's column offsets have no encoder.** `rising_palace.c`'s
  `OFFSET_FOR_BG1 = 0x2000` is the SNES's bit in an offset-per-tile entry
  saying which layer the offset moves; a `vid_*` encoder for an entry (the
  layer, the scroll) would keep the layout in the platform.
- **No service reads VRAM back.** The palace's setup reads the field's map back
  out of VRAM (`$7E:674D`) through `vid_snes_copy` with
  `VID_STEP_VRAM_READ_FIRST`, and the race's setup its plane's column
  (`$7E:3D41`); a `vid_*` read-back (or a copy within VRAM) would take both
  off the SNES-only escape. `ct_native`, which runs both scenes as C now,
  counts them as escapes and leaves the work RAM they read into as it was.
- **Recheck the attract demo after the scene teardown.** The earlier
  `attract-demo` measurement agreed on 9379 of 22499 frames under
  `--vid-check` (PLATFORM.md, "P2 as built"): the then-unported scene
  program's modes 1 and 3 (`$7E:66DD`-`$7E:6CAB`) wrote VRAM, the tile
  maps' bases, the mode and window 1 from
  frames 2789 and 8953, and on two of its world maps BG2's horizontal scroll
  (from 2266, on `$1F5`) and window 1's right edge (from 5492, on `$1F7`)
  changed where no call said so. Repeat the service-fed pixel check after
  replacing the scene escapes above, and locate any remaining writers of
  those two world-map settings. The event gate now passes its service-fed
  pixel check; the old attract measurement does not establish the merged
  scene teardown's result.
- **Renderer performance.** The native renderer was a wash against `render.c`
  rather than faster (the milestone's 81%
  figure predated `render.c`'s line cache), so the speed case for it is still
  unmade. Measure the service-fed path separately from the register decoder;
  pixel identity establishes correctness, not a speed improvement.
- **Host backends beyond SDL**, not yet scoped as milestones:
  - **GPU rendering:** implement integer SNES compositing over `VidFrame`
    and its per-line spans, checked with `--px-check`. Service-fed frames
    are available; remaining transfer escapes still need explicit meanings.
  - **Audio validation:** replay logged game command streams into the C
    driver offline and keep a digest per movie. Its random/synthetic checks
    do not cover exactly what gameplay sends (SOUND.md, "Validating the port").
    SDL2 already supplies the desktop output layer.
  - **Mixer storage:** separate samples from the SPC700-shaped 64 KiB image
    without losing intentional echo overlap. For a small target, reconsider
    `SoundHost`'s two roughly 80 KiB mixers, scratch state reads and fixed
    32 kHz rate; choose resampling and memory limits with that target.
  - **Driver domain:** SOUND.md, "The port as built", records unexecuted
    `$0A34`, `$0B9B`, `$0BD5` and `$F5` loop-pop paths. Define the behavior
    of two queued `$FC`s, song `$3F` overrunning the effect table, and set
    switches under tracks that run into effects; the fuzzer avoids them.
  - **Tinny hi-hats:** the house-song report has no reproduction. If it
    recurs, record the route, songs and C-driver state and compare sound
    memory; the known host sample overwrite is already fixed (SOUND.md,
    "What playing it found").
  - **SNES target:** supply a CPU half to speak to the sound processor,
    from matching assembly or the retired `core/sound_driver.c` in history.
  - **Driver representation:** move `SoundVoiceFlags`' per-track facts to
    track booleans; give signed slides signed storage; decode wave depth,
    tuning, pan and octave units once. Explain `pan_sweep` arithmetic and
    share event control flow with look-ahead, making articulation endings
    explicit instead of side effects of scanning.
  - **Driver duplication:** unify the four slide/fade implementations and
    derive `RESUME_FADE_STEP`; bound `start_effect`'s byte scan explicitly;
    replace `SILENT_TRACK`'s program address with a stopped state. Explain
    tick/loop-count adjustments, instrument/percussion aliases and status
    values. Name envelope/tap constants and phase units; share voice-update
    paths and avoid clearing then restoring song IDs at fade end.

- **Assets as data files rather than ROM reads** (PLAN.md, Phase 4a,
  "Assets", which decides the shape: a catalogue, an import once into a pack
  in the user data directory, loaded by name through the driver, raw before
  decoded; the raw pack is built, PLATFORM.md "The assets, as built"). Still
  open: whether the battle scripts get a readable form before MD3; which codecs the importer
  needs beyond MATCHING.md's ("Build-time extraction").
  - **The catalogue's rows MD2 hasn't typed** (PLATFORM.md, "The assets, as
    built"): 12 `untyped` runs (43954 bytes, `$F6:24B1-7FFF` the largest,
    which the census also lists as unknown) and 32 `mixed` or `undetermined`
    regions now have readers -- the accessors that read them -- and want
    `[[data]]` types with their bounds, which turns their rows into typed
    ones.
  - **The accessors' addresses are keys** (PLATFORM.md, "The assets, as
    built"): each accessor still computes where the original kept its data,
    and the read finds the resource by it. Naming the resource in the
    accessor instead goes with decoding it (PLAN.md, "Assets"); until then a
    pack can't move a resource, only serve it.
  - **Unproved cartridge-address sources.** The conservative
    `py tools/machine_joints.py --cartridge-sources` inventory has 122 sites,
    all linked by `ct_native`: 25 `PF_GAME` sources, 11 source-field
    assignments, one `m_read16`, and 85 generic `wram_read` operands. This
    supersedes the old counter's zero, which only recognized fixed spellings.
    Audit each producer and consumer: prove RAM-only expressions with exact,
    commented allowlist entries, distinguish descriptors later consumed by
    named accessors, or move cartridge-capable reads behind a named accessor.
    No entries have been added to the allowlist to reduce this count.
    Examples needing that proof are the per-line descriptor's selected bank
    in `scene/screen.c`, `menu/epoch.c`'s variable-bank reads, the screen's
    `decompress_dest(g)` transfer, and the stack-relative read in
    `overworld/main_loop.c`. A RAM-looking name or a RAM bank ORed with an
    unbounded expression is not proof. The command lists every remaining
    expression, including computed staging buffers and script operands.
  - **What the asset pack's review left** (none blocking): the `asset
    differs from the cartridge` check compares two copies made from the same
    ROM, so it tests only the importer's address mapping, not the pack's
    content against anything independent; `asset_served` is 7061 slots, about
    56 KB of RAM on a GBA, and every byte read is a binary search (with the
    last hit kept), where a target wants the slots in ROM or a smaller
    catalogue and accessors that name their resource; and the handler table at
    `$C1:7A6B` (`battle/script_data.h`) is code addresses of the original's,
    made a pack resource only because `battle/scripts.c`'s call-outs and time
    tags need them, which goes when those handlers are ports.
  - **The importer's codecs**: the pack is raw; `codec` in the catalogue says
    what a decoded pack would apply to each resource, and nothing applies it
    yet.
  - **Accessors reading work RAM through `game/rom.h`**: a pointer the data
    holds can name work RAM (scripts and text the game copies or builds
    there, `$7E:2C35`, `$7E:98C0`, `$7F:8020-953F` and others over the
    movies), and the cartridge's reads pass it to the machine. A build
    without a machine has to read its work RAM there; `script_language.c`'s
    `script_byte` already tells the two apart, and the others want the same.
- **Open questions** in PLATFORM.md: battles playing out differently once
  timing changes, widescreen's exposed assumptions (the 0-255 object window at
  `$C0:A88D`, map data beyond VRAM, single-byte battle window positions), and
  GBA's constraints.
- **Factory route timing estimates.** `battle-r-series` leaves five timing
  exceptions after drift-only calibration: the anchor jump and enemy orbit
  updates' vblank tails, `field_nmi`'s tail, the exchange's second-choice tail,
  and `battle_step_the_poses`'s +2.96% drift with its own estimate already at 1.
  The jump/orbit and exchange paths vary within a routine; the field handler
  and pose step also carry their callees' estimated time. The narrowed
  video-write check adds no failing finding. Recheck against the combined
  movie library, then refine the estimates that remain over their limits;
  `tools/timing_exceptions.txt` records the measured tails and why they are
  allowed.

- **What the timing service's review left** (none rated translation): the
  objects' turn queue and its guard walk (`queue_in_the_tables`) are written
  twice, in `field/script_turns.c` and `battle/animation.c` -- one
  object-queue type over its head, tail and link accessors; `field/frames.c`
  names the pending uploads and the empty upload queue by bare index and
  value; the older SNES-shaped calls `tm_ack_*` and `tm_reset_beam_latch`
  still return register bits; a call `native_await_latch` sees interrupted
  before the original's own latch goes uncompared.

- **The gate's wait for its song to play** (`$C2:CBE0`,
  `menu/sound_bridge.c`): once the gate's screen has started its song, its
  task spins on the sound processor's status (`au_music_playing`) until the
  driver says the song's tracks are playing, without letting a frame go by,
  so how long it waits is the driver's time to start the song. It is an
  idle entry on the original's machine. As C (`music_waited_for`,
  menu/sound.c) the task looks once a frame, since reading the status passes
  none of the player's time, so it waits whole frames where the original
  waits the driver's time. The native backend wants it as a wait the audio
  service owns: a call that passes the player's time until the song plays,
  and gives up saying where, as the native backend's `wait_on_port0` does.

- **The menus' tasks with no body as C.** Each task's body is one function
  of the game's state whose waits are callbacks, run a turn at a time by the
  machine's entry and through by a coroutine (`core/tasks.c`) with no
  machine; `menu/tasks.c`'s table of the starts it has (`tasks`) holds
  the main task's, the cursor's, the fade in's, the typing's, the scroll
  bar's and the records'. The held cursors', the healing numbers' and the
  gate's song's stop `ct_native` where they start, and a screen step with no
  C column in `menu/screens.c`'s table stops it where it is reached. The other way for the tasks is each rewritten to resume, a state
  machine called once a frame: the way for a target that can't afford nine
  512 KiB stacks (PLAN.md, "What that makes scaffolding").
- **The clock's controller read inside the effect work** (history/reimplementation-chunks.md, "Phase 3,
  battle chunk 4"): the clock's modifier (`$C1:EE27`) runs the frame's input
  work again (`$C2:8002`) mid-work, and the port (`input_take_frame`) can't
  stop where the original waits for the console's controller read
  (`$C2:84F9`) or resets for the reset buttons: it goes on with the sets and
  the play time as they are. No movie reaches either; both want the input
  frame as a `tm_`/`in_` call that has an answer without a wait.
- **What the party on the map left** (history/reimplementation-chunks.md, "Phase 3, field and events:
  the party on the map"):
  - the original adds the leader's contact and the party's objects into a
    place's offset as words, whose high bytes (`field_leader_contact_high`,
    `field_party_objects_high`) the game keeps 0 and the guards require
    so; a build without the original's memory keeps the objects, not the words;
  - regrouping wipes the footprint the members pick the trail up at, and the
    original wipes only its horizontal half and its ground (it stores the
    horizontal parts twice); the vertical half is never read there, since a
    member waiting on a footprint with no ground stands, so a native build
    can wipe it whole.
- **What the leader's walk left** (history/reimplementation-chunks.md, "Phase 3, field and events:
  the leader's walk and the confirm button"):
  - the walk and the screen's recentring call the layers' scroll
    (`$C0:91AC`) and the streaming (`$C0:93E1`) as call-outs on the layers'
    page, though both are ports now: a C call timed as each would drop the
    machine from the walk's entry, once their own rows can be calibrated
    without their own calls (they have no other callers);
  - from chunk 2's review: the four collision nudges of the walk
    (`field/walk.c`, across, along and the two aslant) are near-identical and
    want one `nudge_around`; `look()` slows a climb as a side effect of
    looking, which wants to be visible at its callers. The climb's note
    reuses the layers' wrap cell (`field_layer_edge_wrap`'s low byte), a
    second meaning the map should name. The confirm button's `Search` keeps
    scaffolding-only fields (`passed`, `measured`, `last_measured`) beside
    the result;
  - `field_message_mark` (`$02A1`) is cleared by the chests' message and the
    battle's (`$CD:0264`), and nothing read so far reads it;
  - the walk's guard still leaves to the original a map object count of 0
    or 128, which would start its search on object 0 and go round the slots
    past the tables, and a frame with nobody leading and a step asked for,
    where it indexes the tables with the empty place's marker (`$80`); the
    library makes neither. The counts of 64-127 it makes on a frame or two in
    six movies (112 and 123 at `$7F:2000`) are ports with scaffolding
    (history/reimplementation-chunks.md, "Phase 3, field and events: the map scrolling in"): what
    `$7F:2000` holds on those frames, and whether the confirm button's and
    the moves' searches meet it too (their guards still require a count
    under 64), is still to be read.
- **What the larger sprite layouts left.** Preserve or deliberately replace
  the differing show rules for one object's eight-sprite placement/rebuild,
  and the 24-sprite layout's carry-derived empty result. A renderer that clips
  sprites itself needs explicit rules. Establish whether the last four
  `field_frame_staging_claims` entries have readers before removing them.
  Whole-builder arbitrary-state coverage is part of "The field's
  arbitrary-state validation"; independent field comparisons have retired.

- **What the frame builder left.** In `field/frames.c`, a 12-sprite object
  holding a frame remains at the queue head; native C ends drawing there
  rather than exhausting a beam budget. Decide that policy before replacing
  the staging areas/claims/upload queue with direct frame tiles. Keep the
  guard against `$7F` tile sources wrapping through bank `$80` into low RAM;
  no valid object is known to need that behavior.

- **What the main loop's requests left** (history/reimplementation-chunks.md, "Phase 3, field and
  events: the switches and the main loop's requests"):
  - `$C0:0C76` stops at the field's new start after a location change
    (`$C0:0CC0`, a port: a fresh stack, then `$C0:0000`), and at the
    frame wait or the field's turn the original jumps to at the tile copy's
    and the battle's ends; the tile copy's and the battle's stretches span
    frames, so shadow mode counts some of their calls interrupted and fuzzing
    reaches them only up to their first long call-out (the frame work
    `$C0:00BF`, the battle's setup `$C0:024C`, which run past its budget); a
    fuzz state that bounds those would check the rest;
  - the menus' modes 0, 2, 3 and 6 a script's menu opens
    (field/menus.c, `field_open_script_menu`) are named by their numbers
    until the menus' side names them;
  - the pause and the menus (field/menus.c) call the original's frame wait
    and a character's start (`$C0:597D`); the Start
    assignment's test leaves the overflow of its subtraction, which the
    menus' entry and the next frame read (field/leaves.c);
- **What the screen effects left** (history/reimplementation-chunks.md,
  "Phase 3, field and events: the screen effects and the credits"). One old
  fuzz state in 40,000 differed in OAM bytes resembling the original's stack
  (seed 1, state 10,647). The field and scene comparisons have retired, but
  that receipt does not establish the cause. Reproduce it with a bounded
  native fixture before changing the sprite-buffer or stack compatibility.

- **What the map scrolling in left.** BG1/BG2 row builders end with the
  second half of the tile beyond the right edge, where the first half would
  continue the map. Correct or define that edge for widescreen. Replace
  staging/edge-upload runs with direct tilemap writes only with the same
  visible result. `field_layer_edge_wrap`'s low byte is also the climb note
  and the whole-screen draw's final count; resolve the alias before splitting
  storage (history/reimplementation-chunks.md, "Phase 3, field and events:
  the map scrolling in").

- **What the screen's setup and the objects' setup left**
  (history/reimplementation-chunks.md, "Phase 3, field and events: the screen
  set up, the map unpacked, the objects started"):
  - a switch pressed again after a menu or a battle shows the one-by-two
    switch's upper tile as the lower one's pressed tile (the original sets it
    so, `field/switches.c`): a native build decides what it shows once the
    gate no longer compares the map;
  - the dialog window's look (its frame and text area tile maps, the window
    design's tiles and colours, the choice pointer) is built into work RAM
    and sent as the original's transfers: a native renderer could draw the
    window from its height and position directly.
- **What the scripts set that isn't established:**
  - `field_setting_1e` (events `$DE`/`$DF` raise it with the location they
    set; cleared at `$C0:2842` and `$C0:2866`, returned by `$C0:28A7`, ANDed
    at `$C0:7628`). The field's rewrite reads those routines.
  - The event entries for the menus' services 6, 7 and `$0A` (events
    `$F8`-`$FA`, `$D5`) keep the services' numbers in their names
    (`event_cmd_party_routine_*`); `menu/party.h` names the services.
  Each is named for its address or its position until it is.
- **The field's shadows hold hardware bytes.** The tint (event `$F1`) is
  written as the fixed colour's register byte and a colour-math setting
  ($17 / $97), and the objects' sprite priorities are kept as OAM attribute
  bits: the field's interrupt hands them to the hardware as they are. The
  priorities are values (0-3) everywhere but their storage function
  (`field/object.c`, `object_set_sprite_priorities`); the tint is still
  written in the hardware's terms (`event/screen.c`, `tint_math`), until the
  field keeps it as a `VidColorMath` and converts it at its video calls.
- **What the battle's hand-over reads past the field's tables**
  (`field/battle_start.c`, `field/battle_return.c`). The battle's ground is
  sixteen bytes a row of the map's attribute tables from the screen's corner
  (`$C0:039B`): near the map's right edge a row runs on into the lower
  attributes and the exits, near its bottom edge past the tables' last row
  into the tile looks, through the `_unchecked` accessors. And the field after
  a battle a script sent the party back from (`battle_result` 2) tests the
  event's flags against whatever byte lies at `$7E:0010`
  (`field_after_battle_test_byte`, an absolute `BIT` where `#$10` was
  likely meant). Both are game state a native storage would not have: decide
  the ground past the map (the bytes the battle reads there) and the test's
  bit when the gate retires.
- **`dontcare_check.py` takes a helper's parameters as a cross product.** A
  call-out's or stop's address passed down through helpers is evaluated over
  every value every caller passes, so a helper shared by two kinds of caller
  (the turning and the keep-facing moves) is checked at addresses no call
  makes, and a site read from a struct through a pointer isn't read at all.
  Event's movement bridges were reshaped around it (tables of rows, no zero
  sites); following a table row's index through the callers would let them
  say it plainly.
- **Frame waits still made on the machine**: the races `$C0:3B71` and
  `$C0:4181` (`field/frame.c`), the field's waits for the vblank before a
  change of place made at once (the change after `$C0:3B71` is C,
  `event_go_now_in_the_dark`; after `$C0:4181` it is the original's,
  `$C0:4186`, which no movie reaches): each is an idle entry, a pass at a
  time on the machine, that waits for the vblank through the timing service
  where the original spins on the vblank interrupt's flag; a native build
  wants it a `tm_` wait its port calls (the scene program's frame wait,
  `$7E:31B0`, which only waits, now waits where its callers stand:
  `tm_wait_frame`, `NATIVE_WAKE_VBLANK`, which these could use once they
  are a port's call rather than an idle entry); the idle entries on flags a handler clears
  (`menu_wait_for_frame_loop`,
  `menu_wait_ready_loop`, `battle_wait_window_slide_loop`,
  `battle_wait_graphics_loaded_loop`, `scene_race_wait_for_tilemap_upload_loop`).
  Ports that called the modes' wait routines call
  `tm_wait_frame` (`field_wait_frame`, `battle_wait_frame`). The battle's
  wait routines are ports whose loops give the processor up through
  `tm_wait_for_handlers` (`battle/wait.c`; PLATFORM.md, "Timing without a
  beam, as built"), and the sprite-object mode's and the
  menus' frame waits, which the main thread is in at nearly every frame's
  end, through `tm_wait_for_handlers_held`, leaving the shipping mode's
  waiting to their idle entries (docs/MOVIES.md, "Suspended routines and
  owed time").
- **What the battle's waits left** (history/reimplementation-chunks.md, "Phase 4a, timing: the
  battle's waits and its transfer from the main thread"):
  - The menus' text code the battle message's typing calls (`$CD:04DF` ->
    `$C2:0009` -> `$C2:5823`) reads the carry it is entered with, which
    liveness has dead there (`live_at($C2:5823)` is `V DB D`): scrambling the
    carry at the typing's call (`ct_fuzz --poison-site CD3E89`, the guard
    lifted) changes a VRAM byte's low bit on 199 of 6,982 random texts, while
    scrambling it at the menu's turn (`$C1:0012`, `--poison-site CD3E78`)
    changes nothing over 17,684. The texts that show it are ones the menus'
    text guard turns away (`menu_text_can_carry_on`), so real play doesn't
    reach it, but the instruction isn't found and the analysis isn't fixed:
    find the read, then list the declarations `tools/dontcare_check.py`
    would reject. The shipping mode runs the typing as C (`menu_text_go_on`,
    which reads no carry, behind that guard), and a port reaching a wait
    doesn't carry the original's carry: declaring it live at the
    battlefield's waits mismatched at every battle's (`$CD:132E`, the ports
    above it leave their own), so those keep it dead, and `wait.c`'s call of
    the menu's turn and the transfer's waits keep it live.
  - The waits' fuzz runs stop at the first pass, or at a sibling run as C (a
    real call under `ct_fuzz`, which has no runtime), so a wait's passes and
    the transfer's pieces are checked by shadow mode alone; `ct_fuzz` with a
    runtime, or interrupts it can raise, would reach them.
  - Machine shape the review left: `uploads.c`'s `keep_the_buffer`/
    `take_the_buffer_back` push the original's frame and write X and Y that
    nothing reads, and `leave_the_carry_of_what_is_left` writes the carry
    plainly in the transfer's logic; `wait.c`'s `battle_wait_some_frames`
    keeps its count in A and on the stack. Both exist so shadow mode's points
    line up with the original's stack and registers; they go when the waits'
    callers call them as C.
  - The host's `vid_send_vram_and_wait` stops the program on a game address
    (no host memory behind it yet): the battlefield's change, its one caller,
    passes one.
- **What the battle frame work's remainders left** (history/reimplementation-chunks.md,
  "Phase 4a, timing: the battle frame work's remainders"):
  - The messages, the close and the battlefield put back run inside their
    callers' stretches, which compare them there: the enemy's message is
    compared alone once in 298 calls, the display's close, the window's
    close, the battlefield put back and the setting out never. Their fuzz
    runs stop at the first wait, as the battle's waits' do, so what each does
    after it is checked by shadow mode in its caller alone.
  - Machine shape kept so shadow mode's points line up with the original's
    stack: the bridges say at each wait and call what the original keeps on
    the stack and leaves in the carry there (`BattleFrame`, battle/bridge.h:
    the battle's message's copies of itself, the counted waits' counts, the
    item's place in the rewards' telling, the battlefield the boss's end
    changes to), and the counted waits keep their count in A
    (`message.c`'s `wait_a_frame_counting`, `closing.c`'s
    `wait_frames_counting`). They go when the waits are called as C from
    callers that are C.
- **What the battle frame work's remainders' second chunk left** (history/reimplementation-chunks.md,
  "Phase 4a, timing: the battle frame work's remainders, chunk 2"):
  - The rewards' messages are fuzzed only up to the first message, which
    holds for frames, so the telling after it is checked by shadow mode
    alone, inside the battle's main loop's stretch.
  - The music round a battle (`field/sounds.c`) still sets the field's page,
    the data bank `$00` and A `$0100` on the machine for the driver's calls,
    which read A's high byte; they go when the driver's command entry is
    called as C.
  - What its review left (none blocking after its fixes):
    `battle_wait_music_fade_loop` (`battle/sound.c`) sets the PC by hand, a
    stop-point fragment of `$CD:0D93`'s loop (rubric item 16: port the
    defeat's battlefield change it is a pass of); `battle_pause_blocked` is
    written as `$FF`, 1 and 0 in different files (item 10: a bool, or a
    named state each writer uses);
    and the menu window still reaches the description and the target's
    name (`battle_describe`, `battle_name_a_combatant*`, pure C) through
    `battle_run_long` (item 123: call them as C behind a test of their
    guards, as `battle_play_a_sound_from` does).
  - The review proposed a rubric item for `.claude/skills/review-port/`: a
    guard must test its entry's own preconditions, or say whose it borrows
    (the music's fade takes the waits' guard, `battle_message_can_be_typed`,
    because it runs on into one; `registry.c` says so now). Add it with
    an example once a second case turns up, or now if the owner agrees.
- **What the shipping mode's fallbacks' second chunk left** (REIMPLEMENTATION.md,
  "Phase 4a, the shipping mode's fallbacks, chunk 2"):
  - the Day of Lavos's drawing (`$C6:E7FA`) and its turn's start (`$C6:E90F`)
    run past a vblank, so their one call in `battle-lavos-gameover` is
    interrupted and only fuzzing compares them; a cut of the movie whose
    state starts just before each, run with the interrupts held, would give
    shadow mode a call of each;
  - the scripts' start of an object (`menu_object_cmd_start_script`) is in
    `tools/timing_exceptions.txt` for its calls in vblank in the Day of Lavos,
    whose free-slot search the original makes a slot at a time over dozens of
    running objects: a charge per slot passed at `$C2:048A`'s rate overshot
    every other scene, and wants a rate of its own (the search as a timed part,
    `TIMED_AS` a routine that is only the loop);
  - `$C6:EE31`, beside the turn, nothing in the library calls.
- **What the shipping mode's fallbacks' third chunk left**
  (history/reimplementation-chunks.md, "Phase 4a, the shipping mode's
  fallbacks, chunk 3"):
  - the save screens' fourth step (`$C2:CF92`) is one no code moves the
    screens to, so fuzzing alone checks it; the main menu's save screen's
    opening, the save in a slot and the save itself (`$C2:CEDC`, `$C2:CF0A`,
    `$C2:CF37`) are compared in real play only inside the main task's turn,
    which reaches the original's calls they make, their own comparisons set
    aside (once each in `menus-field-menu` and `menus-end-of-time`); a movie
    that saves over a game and opens the main menu's save screen where it is
    refused would compare more of them;
  - the twinkling stars' guard refuses a pass entered with the carry set,
    which would put the first star it puts anew a pixel further right, and
    which no showing makes; the planet's colours' guard refuses colours over
    the step's code, its working cells or the stack's top, which the original
    would step as it reads them.
- **What the shipping mode's fallbacks' fourth chunk left**
  (history/reimplementation-chunks.md, "Phase 4a, the shipping mode's
  fallbacks, chunk 4"):
  - three entries nothing in the library calls, so fuzzing alone checks
    them: the battlefield changed as an enemy leaves (`$CD:0E05`, an enemy
    whose family changes it, or one an enemy's rule binds), the Beyond Time
    lights' twinkle with the camera turned back first (`$7E:9D45`) and the
    Mode 7 picture sent with BG2's scroll (`$7E:9E19`); a cut of a
    Beyond Time or Lavos fight that meets them would let shadow mode compare
    them;
  - the program's list of large sprites (`$7E:A03D`) in the form that leaves
    out a sprite at the picture's bottom or below: once it has left one out,
    the original goes on with a 16-bit accumulator, storing the next
    sprite's bytes as words and counting down a word, and the guard refuses a
    list that leaves out any sprite but its last; that form's only caller
    (`$7E:9C58`) no showing in the library reaches;
  - the beaten enemies' step (`$C1:006D`), its search (`$CF:FE20`), seeing an
    enemy off (`$CD:0024`) and the dissolve (`$CD:00A0`) span frames, so
    shadow mode compares them only where no enemy is dissolved (12 of 52
    calls) or inside the turn's comparison, and the dissolve's step
    (`$CD:010A`, 3,008 calls) alone on its own;
  - a beaten enemy's dissolve leaves its step's layout, last run and count in
    the battle's shared work cells over the screen effect's kept settings
    (`$7E:0053`-`$7E:0058`), which a later screen effect giving fewer than
    five settings takes up (`battle_run_script_channel` in the Lavos fights
    of three movies' shipping runs): they are written plainly
    (`battle/beaten_enemies.c`), and native storage must keep the two
    variables on the same bytes or give the effect what the dissolve leaves;
  - the fireworks' leftover offset (the last firework's place read as words,
    its Y's high byte the byte past the places) is scaffolding the readers
    are shown not to read (`tools/effects_baseline.txt`, `scene/sprites.c`);
    a native build keeps the offset's high bytes clear there;
  - the screen scripts' drift (`$D7`, `battle/screen_motion.c`) tests the
    high byte of its amount in `$46`, which the last screen work left there.
    The layers' scroll leaves it each frame and most screen commands leave
    it in their logic, both now written plainly (`battle_screen_leftover`,
    `screen_leave_for_the_drift`); a few effects leave `$45` as a word only
    inside their leaves helpers, as scaffolding (among them
    `screen_triangles.c`, `screen_circles.c`, `screen_beam.c` and
    `screen_shapes.c`), and a drift after one, which no run makes, would
    test what was there before in a native build: their leftovers want
    writing plainly too, or the drift's test its own source
    (`tools/effects_baseline.txt`, `drift_high_byte`);
  - what the endings still hand back to the original in the reference play:
    the program's first script step (`$7E:3749`) and `$7E:37CA`.
- **What the shipping mode's fallbacks' fifth chunk left** (history/reimplementation-chunks.md
  "Phase 4a, the shipping mode's fallbacks, chunk 5"):
  - what the shipping mode still runs of the scene program's: the first
    script step's tail (`$7E:3749`, the sequence interpreter going on as the
    original after its wait for the music, `$C3:0C28` being no entry,
    `scene/script.c`) and the title's and attract's frame tasks (`$7E:66B0`,
    `$7E:63D5`, `$7E:64BF`);
  - the frame waits (`$CD:3E75`, `$CD:3E3B`) leave the menu frame's carry,
    declared don't-care on library evidence (`registry.c`): a C menu frame
    that worked its carry out, or a C battle message whose waits don't read
    it, would take the notes out;
  - the victory (`$C1:356D`) goes on as the original after the frame's tail
    where A's high byte comes back set, which the game never makes; its
    fuzzing compares few states (most run past the first call-out's budget),
    and the poses, the victory, the defeat and the turn's joining span frames
    or run inside the step dispatch, so shadow mode compares them only
    through their callers';
  - the Day of Lavos's sampling of the world map (`$C2:6400`) is interrupted
    at its one call in the library, so fuzzing alone compares it;
  - the dactyls: the followers' guard refuses offsets past `NEAR` and a
    leader's spot off the map, which no flight makes; the arrival's x
    measured across the map's height (`slow_so_as_not_to_pass`) is the
    original's and kept.
- **Timing spread in Dalton** (`tools/timing_exceptions.txt`). Drift-only
  calibration updates 226 estimates for `battle-dalton`. The residual
  `battle_find_due_animation` worst error is 53304 clocks (limit 50468) over
  14735 stretches, with mean time 5090 and net drift rounded to zero. None
  of these stretches is in vblank. Its scheduler includes optional frame
  graphics construction in one estimate; measure those paths separately
  when revisiting its timing. This movie's video-write check has no unlisted
  movement. No game logic was changed for this exception.
- **Timing spread in Proto Dome** (`tools/timing_exceptions.txt`). The opening
  and victory poses inherit the frame-graphics and frame-tail estimates:
  `battle_step_the_poses` drifts +9.04% over 256 stretches and
  `battle_strike_the_poses` +4.20% over 262 even with their own estimates at
  the minimum 1. Neither has a vblank stretch. The field continuations from
  confirm and the dialog window have zero net drift after calibration but
  3.93% of 7505 vblank stretches off by at least 1024 clocks (maxima 1949 and
  1137). Diagnose the inherited estimates and the variable dialog/frame work;
  the Proto Dome video-write check found no unlisted movement.
- **Timing spread in the Rainbow Shell trial** (`tools/timing_exceptions.txt`).
  After drift-only calibration, `battle_record_trails` has 1.33% of its 7530
  vblank stretches off by at least 1024 clocks (maximum 1676): its RAM history
  shifts only for active trails whose countdown expires. `field_nmi` has 1.24%
  of 51814 vblank stretches over that threshold (maximum 6273, below the
  quarter-vblank worst limit), with its variable queues, edges and menu work.
  Attribute these variable paths more precisely. The trial's video-write check
  has no unlisted movement. The main menu's opening below also drifts +1.84%
  over 173 stretches with its own scalar already at the minimum 1; its child
  estimates need the investigation described there.
- **The main menu's opening, over-estimated** (`menu_choose_on_main_menu`, on
  `tools/timing_exceptions.txt` for its worst stretch since the shipping mode's
  fallbacks' chunk 7, history/reimplementation-chunks.md): a stretch of the main task's turn
  that opens the menu, drawing its windows and panel, is judged now that a
  port's stretch under the menus' vblank handler isn't taken for the port
  leaving, and its estimate, all of it the menus' own ports' parts at their
  rates, is 163,291 clocks where the original takes 96,084 (once in each of
  `menus-end-of-time` and `menus-field-menu`). Find the part whose rate the
  opening's work doesn't fit (the window copies and the sprite table's
  clearing are the largest), charge it for what it does, and the exception
  can go.
- **What the shipping mode's fallbacks' sixth chunk left** (history/reimplementation-chunks.md,
  "Phase 4a, the shipping mode's fallbacks, chunk 6"):
  - the signed sprite list (`$7E:3A97`) has four callers in the program
    besides the rider (`$7E:7F9C`, `$7E:7FB0`, `$7E:7FBF`, `$7E:8386`) that
    no movie reaches: fuzzing alone compares the lists they draw.
- **What the shipping mode's fallbacks' eighth chunk left** (history/reimplementation-chunks.md, "Phase 4a,
  the shipping mode's fallbacks, chunk 8"):
  - the fallback measure files code by the extent it runs in, followed with
    the stack, and a task's stack lies within its span of another's: code of
    one task that runs after another's port stopped is filed under that
    port's hand-back (`C2:8A45`'s, the gate screen's task and the text's
    under the held cursors'). Ending a hand-back where the stack moves to
    another task's (a jump down further than an instruction pushes, outside
    an interrupt) would file each where it runs, as the stretches of
    comparisons a vblank handler gave up are ended now (`settle_abandoned`);
  - a menu step parked in its wait counts as suspended, so ct_drive takes no
    base while one waits (a member chosen for an item waits as long as the
    player takes), and a quick save then leaves the original's code to finish
    the step (PLAYING.md, "Saves"); the flags of a P a port saved are its own
    there (`native_flags_saved`), which nothing has shown the original's
    code after the routine's return doesn't read;
  - the portable timing service carries SNES mechanics for the held waits
    (rubric item 47): `tm_wait_for_handlers_held`'s `false` ("the backend
    left the wait to the original's loop, return at once") and
    `PF_LOOP_HELD`'s don't-care register set exist only for the SNES
    backend's shipping mode. A host always answers `true` and never reads
    the set; the answer and the set want to move behind the SNES backend
    (the wait's stop made there, as `native_wait_loop_held`'s), leaving the
    service a plain wait;
  - `menu_open_for_sale_list` is in `tools/timing_exceptions.txt` (45% off
    on its one stretch in the library): the list for sale's making is timed
    at its entry's one rate, calibrated on shorter lists, where its work
    goes with the inventory's places. A charge per place listed (as
    `charge_block_move` charges a move per byte) would let the exception go.
- **What the shipping mode's fallbacks' review left** (none blocking):
  - The direct page's scratch cells `$00`-`$0E` are written plainly across
    about a dozen battle files under names that belong to other routines
    (rubric items 87, 114): each plain write wants its value said in the
    game's terms, or a leaves helper, and every leave's offset checked
    against the cells the logic reads by name.
  - `battle/targets.c`'s enemies picked with the stray place
    (`consider_stray`, `pick_enemies_with_stray`) carry the original's A
    through the loop as the next place, for a selector no script uses (item
    34): a guard that refuses the selector, and the stray reads dropped.
  - The tech list's marks are decoded two ways, in `menu_lists.c` and in
    `menu_window.c` (item 18): one decoder, in the subsystem's header.
  - The settings', the item screen's and the opening's bridges still run C
    siblings through `menu_run` (item 123), and the shop's and the techs'
    may yet (their lists' frames, the list for sale and the member rows are
    called as C now): call them as C, timed with `TIMED_AS`, where they make
    no call-outs.
- **The battle's wait for a line `$CD:3B82`**: its callers call
  `tm_wait_line`, whose SNES backend waits where the port stands in the
  shipping mode and calls the original's routine only in shadow mode and the
  tools (`native_wait_call`). Its purpose isn't established: the frame work
  waits for line 64 before the screen script channels run while the timed
  effect slots do (`$CD:09AC`), and screen script command `$C1` (`$CD:1D93`)
    waits for a line from the script. The command's C path already uses the
    timing service, but its five-byte handler has no independent entry: the
    service enters below it, leaving a standalone comparison abandoned.
    Entering through the handler and stopping at the polling loop compares
    the operand but hands the wait back to the ROM, increasing fallback.
    An independent entry needs a service for this tail-called wait, or a
    decision to remove the pacing. Neither caller is followed by a
  register write, so the wait looks like pacing. The rest of the frame work
  is ported around it, and an entry at `$CD:3B82` would make the service's
  own call of the original recurse into the port: decide whether a native
  build keeps the wait at all, and take it out of the service if not.

## Scene continuation boundaries

- **Clean the retired scene entries from the uncompared ledger.**
  Reconcile obsolete scene comparisons, including `scene_race_scale_pictures`
  and the bitmap routines, with the retirement. Audit the timing exceptions
  too: `scene_curtain_jump` still describes a zero-access jump whose comparison
  is gone. Keep any evidence needed by held continuations and native timing;
  remove stale rows only in the appropriate ledger update.

- **Remove orphaned scene wrapper comments and constants.** Deleted entry
  wrappers left their explanatory comments and constants behind, including
  `boot.c`'s `STATUS_BITS` and the `$C3:0530`/`$C3:0538` wait-entry comments.
  Audit `game/scene/` for these remnants and remove only what no remaining
  code or contract uses.

- **The time warp's set-up is owed with a port suspended.** Its pixel-built
  light costs about 110 dark frames charged to the starting field event.
  That event remains suspended in the screen wait: 112 of the gate movie's
  271 warp-handler frames formerly had no drive base or quick save. Recheck
  this after the field's held waits; if it persists, make the setup debt a
  held continuation (REIMPLEMENTATION.md, "The time warp's teardown").

## Battle upload timing

Before retirement, the two Ocean Palace movies' `battle_frame_work`
comparisons exceeded the vblank timing tail: 217 of
11901 stretches differed by at least 1024 clocks (maximum 1537), although its
mean error was 143 clocks and net drift was -0.07%. Its inline alternating VRAM
upload already had a timing exception; the Twins exercised 306 of that upload's
stretches over 1024 clocks. The vblank check found no moved writes. The caller
had a documented `vblank-tail` exception too; retain this sample when
improving the upload's timing, rather than moving the caller's calibrated mean.

## The native program

- **What the field and events' teardown review left.** Remove the three
  `tools/uncompared_baseline.txt` rows for `field_nmi`, `field_irq` and
  `field_walk_a_frame`, whose comparisons retired with the field. Update
  `game/event/entries.c` and `bridge.h`'s comments, which still describe the field
  as gated. Remove the empty control-flow blocks the scaffolding's deletion left
  in `game/field/map.c`.
- **The field's remaining mode interfaces.** Remove boot's retired dispatch
  entry `field_first_start` when its caller no longer needs it.
  The sprite/menu chain also retains the screen setup's Y and overflow
  calling convention. The legacy-only work estimates replace counted
  temporary accesses; remove that backing with the legacy driver.
  The held adapters and called machine forms remain until the legacy driver
  gives way to the native program. REIMPLEMENTATION.md lists each caller.

- **What the batched library ledger's review left.**
  `tools/uncompared_evidence.py` counts a matched trial as complete even when it
  is an intentional early stop: require an explicit count of full returns.
  Separate the dispatchers' ordinary and interrupted timing paths before they are
  recalibrated again.
- **The field's arbitrary-state validation.** Define coherent object/script
  invariants for whole-field property tests. The earlier loose-RAM loop fuzzing
  reached out-of-range character names, event scripts and line-effect tables;
  its three shared failures and interrupted comparisons are recorded in
  history/field-teardown.md. The passing coherent-state recipes are not proof
  for arbitrary RAM. Preserve the empty dispatch boundary and interrupt poll
  while changing the driver. The old `open C0:00BD` and `open C0:18A7` ledger
  allowances await the batched library update; do not prune them on the sample.
- **What the overworld teardown's review left.** `the_cartridges_overworld`
  (`dev/fallbacks.c`) says the menus' helpers the overworld calls are left out,
  which doesn't hold for helpers a handler calls directly (their caller's frame
  is `$C2:0553`): they are still counted, the safe side, so fix the comment or
  the walk. Two retired routines kept no estimate (`overworld_within_reach`,
  `overworld_region_at`; nothing times anything as them): the teardown section
  should say so. The charges taken out of `ORIGINAL_EFFECT` now charge time in
  the native-shape build too, so "the native shape's pace" is approximate.
  `next_frame` in `overworld/main_loop.c` returns a `bool` nothing reads. The
  machine forms skip the run's `--exclude` check `native_hand_entry_at` makes;
  and a comparison of `menu_run_objects` and `menu_object_nmi` of their own on
  the world map would let their two uncompared-baseline lines go.
- **What the object mode's vblank review left.** `turn_on_the_machine`
  (`menu/objects.c`) checks a handler's entry before the return is pushed and
  PC moved, so a guard that reads S, PC or the stack sees another machine than
  dispatch shows it: check the overworld handlers' guards, or push first. Shadow
  mode compares the object handlers and frame steps only inside the vblank
  stub's stretch now, so their own per-entry and `interrupted` counts are gone:
  fuzz them alone (`ct_fuzz --routine`). The long input step (`menu_run_long`)
  runs with no `menu_runs_as_port` check and no `tm_sync` after it, the one
  step whose contract is never checked. The census paragraph it added to
  REIMPLEMENTATION.md reads as a chunk log: keep the mechanism and one
  measurement, move the rest to docs/history/.
- **What the time warp's teardown review left.** `tools/vblank_check_exceptions.txt`
  still names `warp_end`, an entry no longer there: re-attribute or prune it at
  the next whole-library run. `drop_what_the_held_wait_left` doesn't look at
  `handler_away` or at a call-out whose callee yielded to another task's stack:
  assert that can't happen, or skip such entries. With oracle port timing or
  `--exclude warp`, `m_runs_retired_as_c` is still true, so the trip starts as C
  while the cartridge's frames run on scaffolding nothing writes: make
  `native_runs_retired_as_c` false there. The overworld's teardown has the
  same hole (the mode's start sets the world map up as C), and one more:
  its retired continuations sit at the mode's shared frame wait, so where
  the set up is the cartridge's they must not be found by address either
  (`natives_select` would leave them out where the retired subsystem runs
  as the cartridge's). `the_cartridges_warp`'s comment says
  "mirror in $3D" where the code matches `$FD` and `$7D`. The kept estimates
  (`game_kept_estimates`) are frozen, measured by no timing gate or
  calibration; `re/ram.toml` still cites the deleted `warp_on_its_page`.
- **What the map scroll's review left.** `menu_run_sprite_script` underestimates
  the frames that only run a sprite script inline. Its older re-recorded
  outcome lines no longer say whether a protected match was lost; say so.

- **The `dc-evidence:` notes' cited counts are stale.** The notes name how many
  calls their `ct_fuzz --poison-dc` run made, and the current runs make more
  (`game/battle/main_loop.c`'s `$C1:856D` note cites 285; the evidence run made
  697). `dc_evidence.py` still passes; refresh the counts from a run.

- **What the scene frame wait's review left.** `pf_snes_vblank_wait` names
  `$7E:31B0`, not the alternate entry `$7E:31B7`. Establish whether any
  remaining machine caller enters there before adding another encoding row.

- **The data subsystem's storage** (REIMPLEMENTATION.md, "The data
  subsystem's teardown"). The unpacker's gate retired but its storage didn't
  move: its callers name the stream and the destination in work RAM
  (`decompress_source`, `decompress_dest`, `$7E:0300-$0305`) and it leaves
  the length there (`decompress_length`), which the scene program's transfer
  reads. As its callers' state leaves work RAM, `unpack_stream` takes the
  stream and the destination as parameters and returns the length, and the
  cells go with the last caller that reads them. The cells it worked in
  (`$0309-$0316`) nothing of ours writes; `game_uncompared_wram`
  (`game/registry.c`) leaves them out of every comparison, and goes with
  the comparisons.
- **The unpacker's seams with one implementation.** Its callers reach it
  through callbacks made for a machine half that called it out: the world
  map's `Unpacker` (`overworld/setup.h`, used by the overworld, the Epoch's
  screens and the gate screen), which still takes the original's call site
  and a "didn't come back" result it never gives, and the casting's
  `SetUnpacker` (`event/characters.c`). They can be plain calls of
  `unpack_stream`, and the loads' `bool` results lose the unpacking's arm.
- **The unpacker's pace, kept by hand.** The legacy runtime retains
  `retired_rates`, entry-access charges and settles (`state_io.c`,
  `unpack_stream_run`, `unpack_stream_called`) to preserve strict outcomes.
  The native model now charges calibrated unpacking work too. Unify the
  paths only with protected outcomes checked; remove the legacy structure
  with its final caller (REIMPLEMENTATION.md, "The frame-level time model"
  and "The data subsystem's teardown").

- **What core's teardown left on the machine** (REIMPLEMENTATION.md, "The core
  services' teardown"). The console's eight rows (the reset, its copy at
  `$C0:FF00`, the boot, the two vectors and the two stubs) are how the machine
  enters the program, and go with the machine. The controller's wait
  (`$C2:84F9`, idle) and the sample after it (`$C2:84FE`) are a held wait the
  menus' machine path stops in, from `input_update`'s machine form and
  `menu_take_frame_input`; they go when the menus' frame input stops waiting on
  the machine (or the wait becomes a `native_wait_call`), with the reset chord's
  hand-off kept (its unreached `safe` row). Three machine forms stay for
  still-gated callers: `palette_animate` and `tile_animation_upload`
  (`core/machine_forms.c`, the field's and the battle's bridges) and
  `input_update` (the menus' `$C2:8002`); each goes when its last machine-path
  caller does. The palette's and the tiles' `ORIGINAL_EFFECT` scaffolding
  (scratch cells, the multiplier, the pointer, the load's leftovers) is seen
  by those callers' shadow comparisons, and the charges in it are time: it
  comes down with the last of them, not before. `display_blank_and_stop_interrupts`
  (`$C0:0B4E`) stays registered because the reference play's original code
  still calls it outside a port; it retires when the fallback measure shows
  none. The 39 time tags
  (`game/time_tags.c`) keep their last calibration; if a caller's timing drifts
  onto one, there is no shadow run to recalibrate it from, so fit it from the
  callers' stretches or charge the work at the caller's rate. They are a
  second mechanism beside the data teardown's `retired_rates`
  (`runtime/port/state_io.c`); make them one.
- **What the core teardown's review left** (none blocking):
  - `ct_drive` doesn't print `contract broken:`, so the routes aren't held to
    the retired contracts (only the fallback measure's real calls catch a break
    there); print it as `ct_dev` does and have `route_check.py` fail on it.
  - `ct_fuzz` builds its table without `game_time_tags`, so a fuzzed port that
    tags a retired routine counts a bad tag; add them as `frontend/natives.c`
    does.
  - `battle/bridge.c`'s `run_entry` skips `native_poison_sibling_return` for the
    retired forms, so `ct_fuzz --poison-return` doesn't scramble what they leave
    in the registers; give the forms' contracts their don't-care sets and poison
    by them.

- **CI has no no-SDL configuration.** All four jobs install SDL2, so they do
  not exercise `ct_native`'s headless-only build. Add a configuration with
  SDL2 and pkg-config discovery disabled, `CT_ROM` empty and `CT_WERROR=ON`.


What still assumes a machine, as `ct_native` found it (PLAYING.md, "The
native program"; PLAN.md, "The machine out of the product" ).
`ct_native` links the whole of `game/` but `registry.c`, with every service's
host backend, and runs the boot, the field, the title's scene, the menus
a script asks for, the world maps and the battles; left alone it runs the
attract sequence round and on without stopping (the jet bike race and the
rising palace as C), and with `new-game.ctm`'s, `fair.ctm`'s,
`fair-companion.ctm`'s, `gato-battle.ctm`'s or `menu.ctm`'s presses it plays
the movie to its end. What the wrappers and bridges call of the machine is
`runtime/port/no_machine.c`, which nothing `ct_native` runs reaches: its
list, by kind, is what `game/` still asks of one.

- **Boss movies still need native outcome checks.** `battle-mud-imp` runs
  37817 `ct_native` frames with no explicit stop, but ends blanked after three
  screen setups and two video escapes. `battle-heckran` runs 19376 frames
  with no explicit stop, a screen showing after six setups, and four video
  escapes. Both first escape at the title's `$7E:5AEB` (`vid_snes_copy`,
  below). Their fixed-input runs do not establish native boss victories;
  connect the missing video work and verify the intended field outcome.
  Strict mode completes both frame counts but differs from the reference's
  saved game (`drifts` in `tools/strict_baseline.txt`).
- **Castle movie input on the native program.** `battle-castle-slash` (39278 frames) and
  `battle-castle-flea` (35256), with their ctsave06 cartridge save supplied to
  `ct_native --sram`, run all frames without a reported stop, but end with the screen
  blanked after four setups (two title video escapes, first `$7E:5AEB`). Their fixed input does not
  establish the castle outcome on the native program's different timing. Investigate the
  blank screen and drive the native program from state to check the fight itself. The
  shipping `ct_drive` routes do beat both Slash forms and Flea and regain field control;
  strict mode reaches both movies' ends with saved-game drift.
- **Crono's trial movies.** `ct_native` with the normal pre-trial save
  stops `events-cronos-trial` at frame 20629, `$C0:881E`, "a walk the port
  doesn't take", during the prison transfer (the walk guard above).
  `events-cronos-trial-long` reaches its 136868-frame limit without an
  explicit stop, but its screen is blank, with six screen setups; it has
  not demonstrated the full journey in native C. Both runs report two
  SNES-only video escapes, first `$7E:5AEB`. Strict-mode movie inputs
  reach their limits but end with saved state different from the reference;
  neither result establishes a native completion of the trial route.
- **Long movies still desync on the hand ports alone.** The frame-level time
  model (REIMPLEMENTATION.md, "The frame-level time model") charges the C
  sound driver's uploads in both modes and, in `ct_native`, the game's own
  work at the calibrated rates and a load's unpacking and sends, measured as places entered on time (`tools/places.py`). What
  it doesn't hold yet, by what was measured:

  - *Where the game's work is still timed short or long.* `ct_native` times
    the game's own work at the routines' calibrated rates (REIMPLEMENTATION.md,
    "The frame-level time model"); the sample ends 22 of 37 movies as the
    reference does and enters 211 of 293 places on time. The attract demo
    visits all 50 places in order but is up to 344 frames early. What the
    earlier per-routine comparison against the shadow runs' estimates
    (the tags' work in `ct_native` against the parts' and entries' estimates
    over the sample) names next, worst first:
    - *Menu work after playback diverges.* The current sample tags
      `menu_move_among_the_slots` (`$C2:CFA2`) 55,556 times against 718
      reference calls. Beyond Time contributes 51,896 and Death Peak 2,942,
      where neither reference enters that save-menu step. The three matching
      menu runs call it 300, 214 and 204 times in both modes. Likewise,
      `menu_reset_sprites` (`$C2:E92D`) runs 145,789 times against 50,561;
      Beyond Time alone contributes 79,258 against 697. Follow the first
      divergent input before changing the tags in `saving.c::ends` or
      `tasks.c::cursor_task_turn`; their whole-movie excess is not evidence
      that the cost of one call is wrong.
    - *The scaler's timing scopes.* `scene_sample_bitmap_row` (`$C3:034A`)
      has 45,697 native row tags across the two jetbike movies and attract
      demo, against 2,284 calls in those movies' older shadow reports
      (5,160 calls across the full sample). `scaler.c::scale_row` tags each
      newly sampled source row; the machine entry draws a whole bitmap. Compare the same
      bitmap's accesses and the shadow report's attributed parts after the
      scene teardown before changing this tag. Scope counts alone do not
      establish overcharging.
    - *The menus' object-handler checks.* The menus' object handler
      (`menu_run_objects`, `$C2:051D`) reads
      23 times the machine's in the earlier comparison. In
      `objects.c::turn`, `h->takes` runs under the parent timing tag;
      `handlers.c::script_takes` reaches `menu_sprite_list_is_sound` through
      `menu_object_script_can_run`. The machine's `menu_object_script_runs`
      excludes those reads with `vars_peek`. Keep
      the complete guard but exclude its inspection from the native work
      account, then measure the same frames in both modes. This is handler
      time, reducing the main thread's share (at most half a frame).
    - *Short.* The power-on to the title's place (`$1B1` at frame 49 against
      51, strict mode 49) and the load screen's setup (the menus' frame count
      starting at 1720 against 1722; `battle-ozzies-fort` leaves the menus
      with `frames_at_leaving` `$1E` against `$1C`).
    Compare native setup work with the interrupted stretches the shadow
    reports now print. Over the sample, `battle_set_up` measures 78.96 frames
    of work against 78.99 estimated, so its machine calibration is not the
    first suspect. `ct_native --timing-report` now separates each tag's own
    accesses and charges; shadow parent and child measurements overlap and
    must not be summed as elapsed work.
    Beyond Time's sampled replay still misses its ending; after accounting
    for native DMA and unpacking it returns to the load screen at frame 12,304
    rather than 29,845 and changes 4,189 SRAM bytes. Follow the first shifted
    battle input; the aggregate native
    improvement does not establish this movie's outcome.
    Tyrano Lair Walls enters the walkway at frame 3,696 (3,697 before the
    unpacking correction, 3,703 in the reference), then ends with 44 differing
    saved-game bytes: inventory, character progression and battle cursors.
    SRAM still matches and all five places remain on time. Follow the first
    shifted battle input; the re-recorded
    outcome is not a claim that the fight is equivalent.
    Native access, transfer, budget and retired-work accounting need a batched
    ledger update. The expanded sample now includes Ozzie's Fort, Mud Imp,
    Cyrus and Golem Twins, but no library-wide rate fit was made. The historical
    replay audit in history/library-ledgers.md identifies the changed outcome
    pins and native place counts; do not refresh them from the sample.
    The repair branch's final expanded sample ends `FAILED: native; outcomes`:
    all 58 movies complete in strict and native modes and all other checks
    pass, but the shown calibration/model changes still need that ledger.
  - *The handlers' share of an upload.* The sound driver's upload time is
    the wall's under a light vblank handler; under the menus' busy one the
    original's song loads in about twice the frames, and a battle's song,
    loaded once the battle's handler is in, in 56 frames where strict mode
    takes 47 (`battle-mud-imp`'s `$45`, 32,997 bytes sent and 8,964 moved;
    9-12 frames at every battle with a song of its own). Waiting the CPU
    half's own time on the machine (the handlers on top) followed it, but the
    native-shape build's `beyond_time` route then never walked out of `$DA01`
    (its exit search found no move); tried again on the strict-drift branch
    (adding the handlers' time to `native_wait_time`'s wait), it closed most
    battles' start shift and broke the same route (`beyond_time:form2`, the
    second form's chamber, in the native-shape build only), so it was backed
    out. Find why the route stalls before taking it.
  - *A song held while the echo settles* loads at the wait's end, uncharged
    (`frontend/sound_host.c`, `sound_native.c`); only short songs met it in
    the library.
  - *The boot taken with time owed.* A song's start lets the port's time
    pass before the host counts the echo's wait left (`native_sync`,
    `tm_host_sit_out_owed`); the boot doesn't, so the driver booted starts
    (and a boot's own echo wait counts) up to the port's owed time early.
    Syncing the boot too moved `battle-guardian`'s strict run into menu code
    with no port (`$C2:83FB`, frame 16396); port that or find what the sync
    moved before taking it.
  - *The line interrupt switched with time owed.* `tm_enable_interrupts`
    lets a port's time pass first where the vblank handler goes on or off
    (PORTING.md, "A port that spans frames"; `debt_test`); turning the line
    interrupt on or off mid-port has the same hazard and isn't synced yet.
  - *The estimates' error* on the machine: many small errors, each moving a
    frame's end or a frame budget (`tm_frame_time_left`: the field's script
    turns stop at line 72 after a map loads) a little, and a press or a
    battle's turn landing on the other side of it; in `events-cyrus` the
    Northern Ruins' battle starts 10 frames early and the fight goes
    otherwise. Strict mode ends as the shipping mode does; with the
    original's time (`--port-timing oracle --port-horizon interrupts`) the
    drifting movies end as the reference's (`tools/drift.py` shows it per
    movie, with where each parts and the milestone that moved).
    The diagnostic tool must also report failed exits: the ledger's fresh
    original-timed end-state proofs match game and save RAM while reporting
    sound-queue overruns. State agreement is evidence about that state,
    not a passing run; keep the fault visible in the diagnostic verdict.
  - *Dialog presses.* In `events-ocean-palace-lavos`, recorded presses still
    land on different dialog states. After the whole-library rate fit, the
    native replay misses the reference's Mammon Machine room visit (`$19F`
    at frame 16965). Follow the first shifted dialog before adjusting the
    later scene's timing.

  Meanwhile drive the long movies' native runs by their routes (`ct_drive`'s
  scripts react to the game's state, MOVIES.md) rather than recorded presses,
  since no estimate is exact and a battle turns on a frame.

- **What the refusals' review left** (none rated translation):
  `find_the_enemies`'s comment (`game/field/battle_start.c`, "from the object
  past the map's last") is stale since a full map starts at slot 63;
  `stray_slot_is_an_enemy` works out its tile by hand instead of `tile_at()`
  and writes the "there" test again instead of sharing `there()`.
- **What Zeal's fall's and the walk guards' review left** (none rated
  translation): `ripple_the_first_plane` (`game/menu/line_effects.c`) returns
  the phase's carry only for the wrapper's C, which could come from the stored
  phase (`new phase < RIPPLE_FRAME_STEP`); `NOTHING_BELOW_ZERO`'s comment holds
  only for a plane 256 lines tall, so name the plane; `ripple_push` repeats
  `menu_sine`'s sign handling. `note_the_song` (`menu/object_commands.c`) reads
  the song through `menu_object_scratch_answer`, which the map calls a yes/no
  cell: name its second use in `re/ram.toml`. `recentring_fits`
  (`field/walk.c`) works out "starts off the grid" again instead of sharing
  `screen_recentres`'s predicate.
- **What the second story scenes' review left** (none rated translation):
  `after_one_left_out` (`scene/sprites.c`) carries the original's 16-bit A
  flag through the loop. Trying an item on (`menu/figures.c`) now takes strays
  through `item_amount`, `menu_power_of_item` and `boost_of`, but
  `equipment_is_the_games` and its comment still turn them away on the other
  entries: make the two agree. `menu_describe_item` with a stray item wasn't
  audited for an index past its table.
- **What the sound teardown's review left** (none blocking): the fallback
  measure now skips `$C7` code where the cartridge's driver plays
  (`dev/fallbacks.c`); nothing tests that the skip keeps the interrupt and
  callee counts of the code around it, or that `$C7` run under the C host is
  still counted. Add a focused test.
- **What the menu joints' review left** (rated mostly): `script_bridge.c`
  (around the song kept and its neighbour) still routes pure C operations
  through emulated return frames; call their game-state operations directly
  and keep only the register leftovers in the wrapper.
- **What the time model's review left.** `charge_send` in
  `runtime/platform/native/video.c` charges main-thread sends even with the
  picture on, although the model describes blanked loads. Gate it on forced
  blank or establish the size of visible-picture sends. `echoed` in
  `native/audio.c` can advance startup sound before its charged frames pass;
  reconcile those clocks.

- **Cyrus's fixed movie ends blanked in `ct_native`.** `events-cyrus`
  (ctsave14 slot 1, 39872 frames) consumes all its inputs without a reported
  missing C handler, but the display is blanked after 12 screen setups.
  It reports two SNES-only video escapes, first `$7E:5AEB`. Establish how
  far the input gets and whether the native timing misses an interaction;
  consuming the frames does not establish the repaired ruins or restored
  Masamune. The reference route reaches both and returns field control;
  strict `ct_dev` reaches the movie's end with five character-record bytes
  differing from the reference, not a handler stop.
- **Ozzie's Fort movie on the native program.** `battle-ozzies-fort` from ctsave12
  slot 2 plays to its 39237-frame end in `ct_native`, showing, but parts from the
  reference in the field before Flea (`$0B7` entered at 12370 against 7110): the
  load screen's setup, which `ct_native` charged 2.4 frames of the original's
  14.5, is charged 14.5 now, and the game leaves the menus with
  `frames_at_leaving` `$1E` against the reference's `$1C` (`$30` before); the
  field's songs before Flea start 18 and 29 frames early (frames 4399 and 5896
  against 4417 and 5925), where strict mode keeps them to the frame, so it is the
  field's events' work `ct_native` still times short there ("Where the game's
  work is still timed short or long", above), not a port's difference. The
  native run also reports two SNES-only video escapes, first at `$7E:5AEB`.
- **Ocean Palace movie playback.** `battle-golem-twins` and
  `events-ocean-palace-lavos` run for their full 20216 and 30364 frames in
  `ct_native` (exit 0), without a missing-C stop, but both report unsupported video
  calls: four and two respectively, first at `$7E:5AEB`. The finale ends with
  the screen blanked (the Twins run ends showing), so frame completion is not
  evidence that the native program plays these scenes correctly. Diagnose its
  playback and video escapes. Strict `ct_dev --native on --generated off
  --no-interpreter strict` also reaches both ends, with
  58 saved-game bytes differing from reference in each, plus 224 SRAM bytes
  for the Twins; these are `drifts` entries in `tools/strict_baseline.txt`.
- **Yakra's native replay ends with the screen blanked.** The hand movie
  `battle-yakra` (ctsave01 slot 2, 71287 frames) originally stopped at frame
  4032, `$F9:B006`, because the Cathedral's dialogue table was missing from
  the asset catalogue. Adding that table's existing extracted resources
  removes the stop. `ct_native` now reaches the frame limit, reports six
  screen setups and a blanked screen; locate that screen with a state-driven
  run. Strict mode reaches the same limit without a machine-code
  stop, but differs from the reference by 128 saved-game bytes. Input replay
  alone does not establish native completion of the Cathedral route.

- **Zombor's native replay ends with the screen blanked.** `battle-zombor`
  reaches 26320 frames without a hard stop, with six screen setups and two
  SNES-only video calls (first `$7E:5AEB`). Locate the divergent screen with
  a state-driven run; reaching the movie's limit does not prove victory.

- **Nizbel's native replay ends with the screen blanked.** The hand movie
  `battle-nizbel` reaches 57673 frames without a hard stop, with six screen
  setups and two SNES-only video calls (first `$7E:5AEB`). Strict mode also
  reaches the limit, with a 128-byte saved-game difference. Locate the
  divergent screen with a state-driven run before claiming native victory.
- **Giga Gaia's movie in the native program.** `battle-giga-gaia` consumes
  all 33261 frames without a reported machine-only entry, but ends blanked
  after only three screen setups (two SNES video escapes, first `$7E:5AEB`).
  Find where progress stalls; reaching the frame limit does not establish
  the rescue or the field outcome. The same movie in strict `ct_dev` reaches
  its frame limit without stopping, with 572 saved-game bytes different from
  reference play under native timing.
- **Dalton recording in the native program.** `battle-dalton` consumes all
  39666 frames without a reported `ct_native` stop, but reports only three
  screen setups, ending with the screen showing, and two SNES-only video
  escapes (first `$7E:5AEB`). Establish where it loses the reference route;
  this does not establish completion of the Blackbird's destruction.
- **Proto Dome recording in the native program.** `events-proto-dome` consumes
  all 24717 frames without a reported `ct_native` stop, but ends blanked after
  four screen setups and two SNES-only video escapes (first `$7E:5AEB`).
  Establish the first lost transition through Lab 32 and the dome. Strict mode
  also consumes every frame without a halt, but misses the repair: story `$37`,
  repair stage `$00`, Robo absent, 59 saved-game bytes different. Diagnose the
  timed-input drift separately from any native stop.
- **Rainbow Shell trial recording in the native program.** The hand-run
  `rainbow_shell_trial` recording (47547 frames, ctsave13 slot 2; MOVIES.md)
  completes the trial in the reference play. `ct_native` consumes all 47547
  frames without an explicit stop, but reports the screen blanked after only
  six setups. Establish where its inputs cease to follow the reference,
  particularly around the initial settings menu and the courtroom transitions,
  before treating this as a native trial completion. Strict mode consumes all frames
  but ends in the treasury (`$01A`), at trial stage `$02`, without the fragment
  (81 saved-game bytes differ). Diagnose the input drift separately from a
  native stop; shadow has no mismatch or refusal.

- **What the world map still hands the machine as the program runs it.**
  The objects' handlers and the routines a script calls run as rows of C
  tables (`menu/handlers.c`, `overworld/handlers.c`); a handler or a routine
  with no row stops the program where it starts. The menus opened from the
  map go as far as the menus' own C does. `core/program.c`'s handler table
  has functions for the field's, the menus', the sprite-object mode's, the
  battle's (and its closing's), a scene's boot's, a scene program's and the
  warp's handlers; a handler the table doesn't name is still an entry on the
  machine's stack (it saves the interrupted registers, moves the page and the
  data bank, calls out).
- **The scene program's remaining host interfaces.**
  Every mode's setup a recording reaches is C (`scene/program.c`'s
  `c_setups`), and so are the story scenes' tasks, the scripts' routines and
  the main loop's scalings (history/reimplementation-chunks.md, "The native
  program through the story scenes"), and mode 10's picture and the slow
  staff roll, mode 12 (history/reimplementation-chunks.md, "The native program's last named
  stops"); what is left stops the program where a task, routine or command
  reaches a state the ports do not take. The
  stars' first draw in a setup that scatters them starts from nothing in
  particular where the original's goes on from what the call before it left
  (a host's draws take its own noise). The C reads the program's data where
  the boot unpacks it, and doesn't check, as the entries' guards do on the
  machine, that the unpacked code is the one the ports were written for. The
  plane's map halves (`scene_upload_plane_half`, `$7E:876E`), its rest
  blanked (`$7E:5476`, `$7E:5AEB`) and the credits' map blanked (`$7E:90BD`)
  are `vid_snes_copy` escapes the host counts: a send of one byte of each
  VRAM word, from a cursor or a fixed byte, which the video service has no
  call for yet. The scene teardown's census finds no ordinary scene entry
  entered by the machine; these host gaps are separate from its retired
  byte-exact comparisons.
- **A meaningful native run of mode 5.** Its setup, map and figure animation
  are C and every new entry passes fuzzing. No known story path starts it.
  A forced start directly after boot reaches the figure task at frame 41,
  with absent party members outside the seven characters' graphics. Supply
  the field handoff this mode expects and compare its picture and later
  script transitions; do not count the forced boot scenario as a full run.
- **Mode 10's native picture.** The explicit scene run now passes its
  panorama loader and reaches the final scene's scripts in 16,000 frames,
  but reports a dark picture throughout, including the earlier mosaic.
  Compare the native renderer's scene setup and per-line effects against
  the original; the panorama's four entry comparisons pass fuzzing.
- **What the field still hands the machine as the program runs it.** Where
  the field's C meets a state its port doesn't take (an entry's guard, which
  on the machine hands the call to the original) or a step only the machine
  runs yet, `ct_native` stops there and says which: a menu screen with no
  step as C (below), and a party the
  put-aside or the bringing back doesn't take (`field/menus.c`). Every
  bridge in `EVENT_BRIDGES` has its C (`EVENT_BRIDGES_WITH_WORK`, event/engine.h).
  Each wants its C as the program meets it.
- **What the menus the player opens still hand the machine.** The main
  menu's steps and its four panels, and the steps `menu.ctm` reaches of the
  equipment (its opening, the member choice and pointing), the items (the
  opening, the commands, the list and the sort), the techs (the opening, the
  member list laid out, the member choice and the moves among the members)
  and the settings (the opening, the options, an option taken) run as C
  (docs/history/reimplementation-chunks.md, "The native program through the
  main menu"); the save screens and the party exchange do too
  (history/reimplementation-chunks.md, "The native program from a cartridge save"), and so
  do the rest of the equipment's, the items', the techs', the settings' and
  the gate's exchange's steps (history/reimplementation-chunks.md, "The native program
  through the menus the player opens"), and the shops' (history/reimplementation-chunks.md,
  "The native program through the shops"); where one of them meets what has
  no C yet, `program_needs_the_machine` stops the program there and says
  which: an item tried on with the menus' zero word not 0 (`$C2:A5C3`; the
  menus clear it as they open and never write it, and the original marks the
  figures from its place in a table), and a shop whose state a step's entry's
  guard turns away ("a shop the port doesn't take", menu/shop_steps.c).
- **The menus' steps' notes carry machine work.** In the items', the techs'
  and the settings' sequences (menu/item_screen.c, tech_screen.c,
  settings_screen.c) a note is where the machine both leaves the original's
  flags and saves or restores P and sets the widths the original's code has
  at that point (the opening's set-up, the pointing, an option's handler).
  Split these live calling-convention operations from the timing-only notes,
  then remove the notes when the legacy driver's time backing is replaced.
- **What the battle still hands the machine as the program runs it.** The
  battle runs as C from the field's request to the field taken up after it
  (REIMPLEMENTATION.md, "The native program through the battle"), each C
  path behind its entry's guard's state half; where one turns the state
  away, or a call has no C yet, `battle_needs_the_machine` stops the program
  there and says which (71 sites: a comeback shown, an enemy coming into
  the battle past the eight, the two search steps, a demo battle's end, an
  enemy's movement or dissolve the port doesn't run, and the guards' own
  refusals). Each wants its C as the program
  meets it.
- **Two sequences of one routine.** The field's entry, its location's
  loading and song, the main loop's pass, the frame's work, the objects'
  update and view and the pause and menus are one sequence each, which the
  entry and the program both run, their steps callbacks (REIMPLEMENTATION.md,
  "The native program through the field and the title"). Still written twice,
  the machine's beside the program's: the screen's setup
  (`field_screen_set_up`), the objects' setup
  (`field_objects_set_up`), the vblank handler's calls (`field_vblank`), a
  location change's request and a script's menu's (`field_requests_handled`;
  the tile copy's, a step event's and a chest's are one sequence each), the
  time gate's tunnel's loop (`warp_runs` beside its entries, warp/warp.c),
  the scene's frame work and its lists
  (`scene_frame_work`, `run_tasks`) and the event bridges'
  C sends. Each wants the same shape; until then a change to one
  wants the other, and nothing gates the program's copy.
- **Control flow through the machine.** Call-outs into entries by address
  (`native_call_dc`, `native_yield_call_dc`, `native_jump_call_dc`), stops
  at the original's addresses (`m_stop_at`, the `native_*dont_care_at_stop`
  pair), returns through the emulated stack (`m_rts`, `m_rtl`, `m_rti`:
  `native_note_return`/`_rti`, every entry wrapper), and a sibling found in
  the registry by address (`native_hand_entry`) in the five bridges
  (`battle/bridge.c`, `battle/steps.c`, `field/bridge.c`, `menu/bridge.c`,
  `scene/bridge.c`). These go as each subsystem's ports call ports (PLAN.md's
  step 1) and its wrappers leave (step 3).
  Checked forms and held continuations retain each sibling's full contract.
  Some direct targets have stricter bank guards than their callers, including the party's
  return to the field walk. Reset, scene/warp and field reentry tails need their
  shared mode-transition contract before their machine stops can leave.
  Overworld startup's direct first frame also needs a fuzz harness that supplies
  interrupts: all 200 existing setup states now exceed its comparison budget,
  although seven targeted movie calls compare cleanly.
- **Evidence worker count is not a process cap.** `tools/dc_evidence.py`
  can start four processes per movie, so `suite.py -j 2 --evidence` can run
  eight. Share one process budget across variants and movie workers; a
  worker count alone does not enforce the repository's two-process limit.

- **The machine's memory and registers.** Remove the wrappers' bus accesses,
  register/page/bank/stack state and guard peeks with the legacy driver.
  `ct_native` uses host memory but still backs accessors with the original
  128 KiB byte layout (`runtime/port/state_host.c`); moving it to structs
  requires the alias and overrun decisions in this file.

- **Time charged to the runtime.** `NATIVE_TIME_AS` and its kin
  (`native_time_as`, `_end`, `_sibling`, `native_uncounted_*`) take the
  machine: tags for the timing gate. They want the game's state as their
  handle, as `TIMED_AS` (charge.h) has, whose host half is a no-op.
  The field, battle, overworld, event and controller now use `TIMED_AS`
  for ordinary timing scopes. Battle's three `NATIVE_TIME_AS_SIBLING`
  scopes and the field's returning-pass scope still need a state-based API
  that preserves sibling accounting.
- **Waits on the beam.** `tm_snes_video_status`, the SNES backend's read of
  the beam's status, from the idle entries' passes (`field/frame.c`,
  `scene/boot.c`); and the remaining wait loops passed on the machine,
  especially the menus' ready loop `$C2:81CA`, each wanting the timing
  service's wait ("Timing without a beam", above).
- **Code run from RAM.** The scene program runs as C, but still unpacks
  its program image for scripts, sprite lists and tables. Extract those
  resources by name before removing the image. `core/program.c` also reads
  the interrupt stubs as data; replace their ROM-address keys with handler
  identities when the legacy driver leaves.

- **Ports that name a register.** The SNES escapes have host halves
  (`runtime/platform/native_escapes.c`) that follow what a host can: the field's memory clear (`vid_snes_copy` from the
  multiplier's product) clears the memory, the sprites' setting a script
  writes by its address (`vid_snes_write_register`) is the setting, and the
  battle's palette sent into memory (`vid_snes_send_palette_into_memory`)
  is sent. The rest of `vid_snes_copy`'s and `vid_snes_transfer`'s uses
  (`battle/uploads.c`, `menu/display.c`, `scene/race.c`,
  `scene/rising_palace.c`, `scene/screen.c`, `scene/staff_roll.c`;
  tests/test_platform_lint.py's list) are transfers into VRAM programmed from
  the game's tables, which only the SNES's channel settings say the meaning
  of: the host counts them and the driver reports the first.
  In the assigned battle files, two escapes remain. Battle's bank-dependent palette send intentionally
  writes stray RAM and delays the palette; those writes need named state
  before replacing the escape. The pending window transfer's only writer
  requests VRAM words, but the entry accepts arbitrary destination/mode
  descriptors; a VRAM-only replacement needs that guard contract settled.
  The three Death Peak movies reach their full 4676/23852/23856-frame budgets
  in `ct_native` without stopping, but report 2/4/4 video escapes, first at
  `$7E:5AEB`; wind and ascent finish blanked, return showing. These fixed-input
  runs do not establish the route outcomes under the host's different timing.
- **What the host backends don't do yet.** The native player already sends
  audio through `sound_native_take_audio`. Unify `frontend/sound_native.c`
  and `sound_host.c`'s echo waits and command queues when `ct_play` adopts
  the native driver. `au_driver_call` still stops on a machine-wrapper call;
  remove that interface with its remaining callers.
- **Reads past a table's end, as the original's glitches make them.** The
  original reads and writes past its tables where a player can push it there
  (the equipment screen with 98 slots of one kind reaches memory far outside
  the inventory; docs/movies-wanted.md, Targets 14). The gate fails any
  `accessor out of range`, and `ct_native` keeps the game's state in host
  memory rather than a SNES work RAM image, so what lies past a table's end is
  undefined there. To decide: lay the native state out as the original's work
  RAM wherever an accessor may run past its end (the glitch reproduced byte
  for byte), or name the reachable overruns, make each a defined, bounded
  result, and leave the arbitrary-memory glitches out of scope.
- **What the story scenes' and the second native gaps' reviews left:**
  - The C paths of events `$D5`, `$D6`, `$29` and `$FF $96` (the game's
    restart) are fuzzed only through their machine entries: no movie reaches
    them, so the C itself is unexercised.
  - re/ram.toml's `unchecked` notes for view, role and solidity cite only the
    walk, not `object_in_the_way`.
  - `menu_draw_empty_label_entry` saves P and returns without restoring it
    when refused (unreachable behind the registry's guard).
  - The credits' `CreditsLettersContext` callback always returns true and
    `event_choose_credits` ignores the sequence's result: ceremony.
  - `MENU_LABEL_RECORD_BYTES`, scaffolding-only, sits in the public
    menu/load_screen.h.
  - The scene engine's three `step_the_ground_toward_*` script rows are one-line
    rows over `step_the_ground_toward(g, set)`; folding them into one row wants
    a field on `SceneScriptRoutine`.
- **What the native picture's review left** (the line handlers' end of the
  picture, `runtime/platform/native/video.c`, `tools/native_check.py`):
  - The dark check judges only a movie's end and never against the reference
    play: a reference that rightly ends 10 s on black would fail for good, and
    a long dark stretch mid-movie is reported but passes.
  - `native_check --check` doesn't hold "stopped" to a baseline, so a movie
    that starts stopping earlier in `ct_native` passes; a ratchet would.
  - `scene_boot_line_irq_blank`, the boot's machine wrapper, still calls
    `vid_set_display` where its C path now calls `vid_raster_display`.

## Refusals in real play

Guards that turn away a call real play makes. On the machine the original
runs the call, so shadow mode and the shipping mode pass; the hand ports
alone (`strict_check.py`) and `ct_native` stop there. Each is a gap line in
`tools/refusal_allowlist.txt`, which comes out when the port takes the state.
The standard library has no allowed refusal. These hand-run findings still
need diagnosis; they are not allowlist entries or passing contract evidence.

- **600 A.D. Guardia Forest's object searches.** In
  `events-cronos-trial-long`, `field_object_in_the_way` (`C05B95`, 320
  refusals) and `event_cmd_move_to_tile` (`C04F61`, 280) first refuse at
  frame 82179, entering the forest after Frog leaves. The latter's walking
  guard checks `object_search_fits`; the former also checks the object's
  facing. `event_cmd_move_toward_party_member` (`C05410`, 160) first refuses
  at frame 84020, after the first encounter, through the same movement
  search guard. Diagnose the map's object-search bounds before widening
  these guards: the map holds all 64 objects, so a search that starts at
  the count starts one past the tables, as the battle's start does
  (`field/battle_start.c`, `stray_slot_is_an_enemy`).
- **Leaving Guardia Castle in 600 A.D.** `overworld_lead` (`C23404`)
  refused once at frame 81958 of the long movie, with `X=0DF0`,
  `D=0000`, `DB=00`, `P=24`. The overworld has retired the gate since, so
  its guard is a contract and this is a `contract broken:` line now (the
  call still goes to the original, as the refusal did); the movie is not in
  the library. Inspect `overworld_leader_takes`' walker, animation and
  map-following constraints at the exit.

## Review follow-ups for native interfaces

- **What the battle and menus' teardown review left.** `overworld/setup.c`'s
  `leave_blocks_counted` is kept for the Epoch's comparison, which has since
  retired: find a reader or delete it. `tools/machine_joints.py`'s
  scaffolding count of 13 is eight call sites plus five occurrences in
  `original_effects.h`'s and `original_frame.h`'s macros and examples; count
  call sites alone.
- **What the native program's review left** (none rated translation):
  `core/program.h` and `program.c`'s handler table are keyed by the ROM
  address a mode installs, where they want a handler identity; `dev/census.c`'s
  `census_destroy` leaves the runtime's observer pointing at the freed census;
  `state_host.c` and `game/data/assets.c` each carry the HiROM address map,
  which wants one copy.
- **What the one-copy review left** (none rated translation): in
  `object_update.c`, `UpdateCall`'s `RESORT_SECOND`/`RESORT_FIRST` and the
  `call` argument threaded through the view, tick and rows exist only to pick
  the machine's call sites, which its own context could track;
  `object_view.c`'s `took_what_it_needs` callback exists only to clear V, which
  the wrapper can derive from the state after the call; `main_loop.c`'s
  `frame_work` rows mix the machine's columns (`long_call`, `x8`, `entry`) with
  `run`, which wants a parallel table, and its eight one-line C
  adapters want each C step its own signature.
- **What the menus' tasks review left** (none rated translation; `naming.c`
  and `save_screen.c` the weakest): their register notes
  (`BUTTONS_TESTED`, `NOTE_CHAPTER_FOUND` carrying the compare's operands) and
  call-site addresses ride through the shared logic, where the machine-side
  callbacks could derive them, and the `site` parameter hides them from
  portsmell's flag-note check; `cursor.c`'s `bool *fast` and
  `screen_setup.c`'s `uint8_t *shown` out-parameters exist only for the
  wrapper's registers; `screens.c`'s dispatch by routine address repeats
  `naming.c`'s `steps[].routine`; `techs.c`'s guard
  `menu_party_techs_can_be_noted` tests a high byte only the original's code
  leaves, which with no machine is always the power-on clear's 0, so in
  ct_native it guards nothing: say so, or drop the test from the C path. The
  review rubric wants an item for scaffolding notes routed through a steps
  callback.
- **What the world map's review left** (none rated translation):
  `field/requests.c`'s `TileCopyCall` carries the original's 33 call sites
  through the logic, where it wants the routine and layer and the bridge keeping
  the sites; `overworld/main_loop.c`'s `arm_entered`, `script_started` and
  `cpu_stilled` hooks exist only to set registers the screens the loop
  calls on the machine read (the overworld's teardown took
  `flags_given_back` and `looked_up` away with its scaffolding); `menu/frame.c`'s
  `bool *sent` exists only for A's leftover; `menu/line_effects.c`'s
  `FAR_CALL_X`/`NEAR_CALL_X` feed the dispatch's X into `step_the_curtain`'s
  `salt`, which wants naming for what it is and the comment there agreeing.
- **What the screen effects' review left** (none rated translation): a `$E1`/`$DF`
  in a map's script at the load hands back, and `field/main_loop.c`'s
  `field_runs` returns without asking `program_field_starts_again`, so ct_native
  stops at the field's entry with `field_again` set; the pass loop's handling
  wants applying to the load too. `field/screen_effects.c`'s `Moment` is ignored
  by every C callback and exists only to pick `calls_at`'s call sites;
  `moment == RIPPLE_GROWING` deciding when the circle opens wants a column;
  `calls_on_the_field` repeats `move_on_a_step`'s choice between the curtain,
  the close and the ripple. `main_loop.c`'s `effects_refreshed` and
  `frame_waited` are `screen_effects.c`'s `refresh_the_effects` and
  `wait_for_the_frame` again. `event/party.c`'s `ask_after_character` works
  out `leave_jump`'s target instead of reusing `after_asking`. The credits'
  page setup has three names (`scene_credit_pages_set_up`, `_entry`, `_scene`).
  `field_layer_drawn_at_the_load` wants a `FieldLayer`, not `unsigned`.
  `scene/screen.c`'s `scene_animate_the_palettes_for` records a stop and
  carries on with the channel's sequence, and `scene/rising_palace.c` ignores
  the palettes' check (it can't fail there, but says nothing).
- **What the battle's review left** (none rated translation):
  `battlefield_change.c`'s
  `battle_load_and_run_battlefield` and its machine twin each spell out the
  load, the scripts' start and the running flag, and `closing.c`'s fade then
  wait is written twice. The waits (`wait.c`) check the typing's fit only where
  typing happens, which wants saying. `tools/timing_exceptions.txt`'s
  `battle_frame_dispatch` line (+0.42% against 0.38%) names the restructure,
  not the callee whose time moved onto the dispatcher. `tools/dontcare_check.py`
  counts every identifier in a body as reached for `dc-evidence`, which could
  be argument positions only. The combinations' C path checks
  `battle_window_fits`, stricter than its registry guard (the page alone).
- **What the race's tasks' review left** (none rated translation): the
  curtain task is written twice (`scene/race.c` and `scene/rising_palace.c`
  each define `CurtainInC`, `curtain_step` and a near-identical sweep; one
  helper in `curtain.c` taking the slot and how to show the window would do
  both). `(TaskEnd){ TASK_LEFT_TO_THE_ORIGINAL, NO_TASK }` is spelled inline in
  four files and wants a `task_left_to_the_original()` in `scene.h` beside
  `task_goes_on`. `race.c`'s `race_sounds` table keeps a column of ROM call
  addresses that only `race_sound_on_the_machine` uses; it belongs in a table
  beside that. `rising_palace.c`'s `turn_the_first_channel` runs
  `TURN_CHANNEL`, which names the channel holding the palace's turn registers
  and works only because both are 0. `perspective.c`'s `start_is_a_scene_s` wants
  a better name, `race_camera.c`'s `core/program.h` include sits in the wrong
  block, and `frame.c`'s `channel_0_turn`/`channel_1_turn` twins want one row
  carrying the channel.
- **What the main menu's review left** (none rated translation): the menus'
  `*Place` enums (`MainMenuPlace`, `EquipmentPlace`, `ItemsPlace`,
  `TechsPlace`, `SettingsPlace`) are the original's call sites one to one, and
  the C side ignores them (the naming's `NamingNote` is the precedent).
  `main_menu.h` declares machine types and helpers (`MainMenuOnTheMachine`,
  `menu_main_menu_on_the_machine`, the `menu_leave_*_panel_*(Snes *)`) that
  belong in a bridge header. `settings_screen.c`'s `handlers[]` repeats the
  bridge's `OPTION_HANDLERS` routine column; one table, or the bridge takes
  the kind from the logic. `menu_settings_can_open`, `_can_choose` and
  `_option_can_be_taken` are declared in the logic header but defined in
  `settings_screen_bridge.c`, and `can_open` spells the window list as
  `0xC1F6`. `tasks.c` loops on the task's ROM address and uses it as the
  `TIMED_AS` tag, where a first-pass flag and two literal tags would read
  better.
- **What the save screens' review left** (none rated translation):
  - `saving.c` works around name clashes with `menu_show_the_slot_saved_now`
    and `menu_put_save_panel_away_now`: the C functions want the plain names
    and the machine ones `*_entry`. Its two `// $C2:CF21` around a forwarder
    have `SLOT_HOLDS_A_GAME` stranded between them. Machine entries named
    nearly like their C logic want `*_entry`: `menu_ask_which_to_carry_on_from`
    (beside `menu_ask_which_game_to_carry_on_from`), `menu_draw_slot_window`
    (beside `menu_redraw_slot_window`), `menu_draw_load_screen`.
  - `LoadScreenPlace`, `SavingPlace` and `ExchangePlace` are the original's
    call sites one to one, as the main menu's are, and some notes run only
    under a game branch (`menu_show_save_slot_with`'s fourth row).
  - `tasks.c`'s held cursors' table spells its resume addresses as bare
    literals beside named heads, and computes its timing tags.
  - `load_screen.c` reads and rewrites one byte of a word (`& 0xFF00`), which
    wants the RAM map narrowed.
- **What the field's stops' review left** (none rated translation):
  - `field/requests.c`'s C path checks the requests' guard after
    `field_step_screen_changes`, the machine entry's guard before it, and the
    guard's `field_is_dark` can change with the fade's step: check whether the
    machine guard lets a location change through, on the fade's last frame,
    without checking the leader is readable.
  - `warp/warp.c`'s `kind_of` spells the warp kinds as `WARP_FIRST_SCENE + n`
    where the field names them (`SCENE_LONG_WARP`, ...); one definition. The
    trip's end is still put together twice (`warp_runs`, and the held wait's
    `end_the_trip_on_the_machine`).
  - `scene/planet.c`'s `planet_unpacks` keeps the machine's ROM call sites in
    a column beside the resources; a table only the machine step reads. Two
    comments sit back to back above `PlanetUnpack`.
  - `menu/turning_world.c`'s `world_can_be_turned` takes `self` and reads
    `menu_object_current` instead; `hand_the_turn_on` returns the unsigned
    add's carry as "went round past a whole turn", which wants saying for a
    negative turn.
- **What the last save stops' review left** (none rated translation):
  - Steps enums name one routine split by call site, so the machine table can
    pick the site (`EQUIPMENT_REDRAWS_THE_LIST_FOR_THE_SLOT`/`_ON_THE_SLOTS`,
    `EQUIPMENT_SLIDES_THE_MEMBER_ROWS_UP`/`_DOWN`, `TECHS_SLIDES_THE_ROWS_UP`/
    `_DOWN`, `ITEMS_OPENS_THE_MEMBER_WINDOW_FOR_A_TONIC`/`_TAB`,
    `ITEMS_USES_ONE_MORE_TONIC`/`_TAB`); the logic's enum wants the routine,
    the machine side working the site out from its own context.
  - `registers_at` places that only bracket a `run` (`*_SLIDE_STARTING`/
    `_ENDED`) belong in the machine's `run` for that routine, as the techs'
    targets reopening now does; equipment has 27 places. The exchange calls
    the same hook `slide`/`widths` and carries non-slide places on it; one
    name across the screens.
  - `menu_raise_amount_kind_with` works the kind out from cells and ignores
    the kind `run` passes.
  - `menu/epoch.c`'s machine steps still `m_jsr` into the file's own
    registered ports (`$C2:6965`, `$C2:63DF`, `$C2:7389`, `$C2:71F9`).
  - `event/party.c`'s `fill_with` takes a `bool more` only the machine's
    leftover reads; `equipment_bridge.c`'s scaffolding loads unnamed literals
    (`0x42`, `0x62`, `0x2E`).
  - `menu/sound.c`'s `music_waited_for` waits a frame per check on the C path
    with no limit, where the original waits the driver's own time (the audio
    service that gives up with a message is the item under Platform).
- **What the shops' review left** (`menu/shop_steps.c` mostly, the rest hand):
  - The C callbacks share their `ShopSteps` members' names (`read_cursor`,
    `tick`, `wait`, `registers_at`, `run`, `note`, `ends`), as the equipment's
    and the main menu's do; `dontcare_check` misreads a callback named like
    its member.
  - Locals kept only for the scaffolding's notes (`left_by_the_last`,
    `shown_before`, `total_before`), two bytes packed into one note, and the
    literal register loads (`note(..., LOADED, 0x13/0x80/1)`) belong in the
    bridge's leave helpers.
  - `DEAL_SOUND` is defined in both `shop_steps.c` and `shop_bridge.c`.
  - Trying on (`$C2:E162`) runs after a move along the list without
    re-checking its own guard; safe while the move leaves that guard's state
    alone.
  - `ShopPlace` and `ShopRoutine` have a member per call site (the panel four
    times), and `for_the_list` adds to the enum to reach the selling member:
    the same split as the last save stops' first item.
  - `menu_figures_are_workable` (`figures.c`) repeats the test of the new
    `menu_figures_can_be_worked_out` rather than calling it.
## Targets beyond the desktop SNES build

These need target-specific designs after the asset, state and machine
boundaries in PLAN.md. Their host drivers can use the existing frame/input/
persistence seams; game-side representation changes still need explicit work.

- **A WebAssembly build.** Implement a driver with a frame callback,
  audio worklet fed by `audio_fill`, and browser persistence through the
  player's byte/name API (PLAYING.md, "How the player is built"). The native
  program removes generated-ROM-code distribution and per-port fibers from
  the design; import assets from the user's ROM at runtime. The main thread
  and menu tasks still need stack switching, Asyncify or resumable state
  machines. Evaluate memory and speed on that native build before choosing
  a fiber backend; this target is not scheduled (PLAN.md).

- **A native AYN Thor multi-screen port.** The Android/ARM64 driver can
  provide pad input through `in_host_set`. Decide whether the second screen
  is a frontend companion view over game state or a second game screen;
  `VidFrame` currently describes one screen. The latter needs service and
  game-side layout changes, including P4's exposed-edge decisions. Both can
  consume service-built spans (PLATFORM.md, "Video from service calls, as built").

- **DS and PlayStation content in the original.** Extra dungeons, arena
  and bestiary need content authoring and an asset pipeline that accepts
  resources without ROM addresses. The field, events and scene engines are
  now C; decodable, replaceable data is the next prerequisite. Define formats
  from the intended reference and keep extracted assets out of the repository.
  Animated cutscenes require a separate video-decoding design; no runtime
  path supplies it.

- **A SNES ROM compiled from the hand ports.** First make matching source
  relocatable (MD3: symbolic pointers and shift tests), then link one compiled
  routine beside the remaining assembly and validate play. Keep logic apart
  from calling-convention glue; `tests/test_port_subset.py` enforces the C
  subset and the SNES service backend supplies hardware access.
  Evaluate the 65816 compiler, calling convention, licence, code size and
  interrupt budget on that hybrid before expanding it. Hot routines may
  need assembly; the nearly full 4 MiB image may need an extended layout.
  Preserve cartridge-save compatibility and the sound CPU interface.

- **Native touch controls**, beyond a virtual pad. A pointer service needs
  position and press state, then explicit hit-test rectangles for menu and
  dialog choices. The menu code is C but tilemap writes alone do not define
  those rectangles. Choose a field policy (tap-to-walk or synthesized
  direction) and preserve signed screen coordinates for widescreen; the
  object activation window at `$C0:A88D` is one boundary to revisit.

## Movies and coverage

- **What the library doesn't reach, measured.** Extend play coverage using
  movies-wanted.md's current inventory and `tools/unreached.py`'s ledger.
  Retired scene/field inventories retain evidence for paths never reached by
  a movie; a clean bounded fuzz recipe is not an end-to-end gameplay check.
  Recompute coverage from the whole library before claiming a path unseen.

- **Sound-command echo stall in The People of the Times.** The hand-run
  `people_of_the_times` route (technoplaza ct-state21 slot 2), with all fight
  seeds 1, wins Lavos and reaches `$05F`, then stays black before The End.
  At frame 181083 its object 1 is at script offset `$033D`, wait count 0,
  and the CPU cycles through `$C7:0635`-`$063B`, waiting for the sound command
  echo; the SPC700 cycles at `$101E`/`$1024`. Another 9000 frames and probes
  with A, B, Start, Up, X and Y leave it there. Replaying from the Core with
  seed 2 wins at 149750 and finishes at 160570; that is the route's default.
  This is a reference-runtime stall, not a guard refusal. Its cause is not
  established; no game code was changed to work around it.
- **Content no movie plays at all.** Use movies-wanted.md for the remaining
  route targets; many of the old sidequest and boss gaps now have movies.
  Reuse the pathfinder, dialogue and fight helpers in `tools/movies` and add
  puzzle/minigame policies where needed. Re-run routes after timing changes:
  state-driven routes survive drift, their recorded inputs do not.

- **The R Series and Nizbel II fights** (movies-wanted.md, "Boss fights") are unattempted beyond finding
  where each save starts. `ctsave03` slot 2 (R Series, the
  factory) and `ctsave07` slot 3 (Nizbel II, Tyrano Lair) start on the world map
  at the dungeon's door, needing a walk in and
  down to the boss room neither attempted nor mapped this pass.
- **zophar `chrono-7`** (everything maxed at the fair) is unused: its Lucca
  exhibit gate (`$7F:0055` bit 7) can be opened, but no movie needs it yet.
- **Coverage loop**: re-run `--misses` over the whole library after the latest
  movies and feed anything new back into the build.
- **Snes9x agreement tables** in RUNTIME.md were re-measured after the timing
  fix for `new-game`, `menu` and `field` only; `fair` and `fair-companion` got
  worse and are unexplained.

## Player and tooling

- **`tools/portsmell.py`'s `sibling-through-machine` matches only literal
  addresses in `m_jsr`/`m_jsl`**: a call-out to a registered port through a
  named constant (`rising_palace.c`'s `m_jsl_dc(s, ..., CURTAIN_STEP, ...)`)
  or through the bridges' `battle_call`/`menu_call` gets past it, and so does
  `menu_run` of a C sibling. Resolving the enum constants is cheap; telling
  the findings from the legitimate cases is not: a `battle_call` to a port is
  right where it is the call-out a guard's refusal makes (`battle_take_turn`,
  `battle_take_step`), and `menu_run`'s targets are ports by design, so only
  a sibling with no call-outs is item 123. The check needs to know which
  calls are a refusal's (a helper's body, or a branch on the guard) and which
  siblings call out.
- **The fallback measure's edges** (REIMPLEMENTATION.md, "The fallback measure"):
  - it follows extents by the stack, so a task switch to a stack within
    `STACK_SPAN` of the one it left (`dev/fallbacks.c`) keeps the extent it
    was in, and a routine reached by a jump rather than a call is filed under
    its caller;
  - an interrupt whose handler moves to a stack of its own (the menus'
    vblank handler) keeps the extents under it until S is back above where
    it was taken (`handler_away`); a handler that never comes back leaves
    them under the code that follows, which then counts as code with no
    call on record. None in the library does, and the runtime's rule for a
    call-out under such a handler, and for a port's own stretch under one,
    is the same (`Active.handler_away`);
  - a comparison the runtime gives up under such a handler keeps its
    stretch, uncompared, until its routine returns or reaches where the port
    stopped (`abandon_under_the_handler`), and only where the port's run had
    ended when it was given up; a port that calls out after its stretch's
    vblank would hide the callee, which the shipping mode's ratchet
    (`tools/fallback_shipping_baseline.txt`) is there to catch. The measure's reasons in
    tools/fallback_baseline.txt for `C2:----`, `7E:----` and the other lines
    the sample lowered still hold the movies outside the sample at the old
    measure's counts until the library's batched run takes them;
  - an idle entry's pass that leaves its wait is taken back only where it
    ran nothing but the loop and a port takes over at the exit, or the entry
    returned within its entry's bytes (`idle_return_span`, set on the scene
    scripts' wait for the music); the other idle entries' leaving
    passes (an exit that goes on into the routine) stay counted;
  - the Beyond Time route's replays from a base grew from 30,056 frames to
    44,660 with the battle frame work's remainders' second chunk
    (REIMPLEMENTATION.md, that chunk's "Measured"), which the route measure no longer counts
    (it counts the frames kept) but which still cost the route's run its
    time: check whether the new ending ports (`$C0:75A0`, `$C0:6E27`,
    `$C0:0AFF`) leave estimated time owed at frame ends, which thins out the
    bases the search replays from (the likely, unconfirmed cause);
  - the shipping mode's count is ratcheted only loosely, as a subset with
    slack: it moves with every port's timing (99 of 1068 keys between the
    default and the native-shape build), and its baseline was seeded over the
    gate's sample and the long routes, so the library's batched run may add
    lines for the movies outside it. A shipping-mode run that played the movies at the original's timing
    (`--port-timing oracle`, where it fits) would be the closer measure of
    what a player runs.
- **The movie suite's wall time is the longest movie's.** Measure the
  current sample's critical path before optimizing the gate. Remaining ideas:
  split long movies at reference-state checkpoints and account for in-flight
  comparisons; share a boot-prefix snapshot; avoid copying irrelevant sound
  memory; select movies by changed-code reach and reuse verified evidence.
  The sampled gate and evidence cache already reduce repeated work. Their
  keys and process budget have separate follow-ups; do not require a whole
  library run on every branch (PORTING.md, "What shadow mode costs").

- **The burndown ranks by bytes, and can't say a run is blocked.** Show
  dependencies on untyped resources or unsupported domains alongside a run's
  byte count. The old `$C2:27DE` loader example is ported; the remaining
  unsupported domains are in "Uncovered runs outside the scene program".

- **`ram_map.py coverage` stops measuring a file once it is converted.**
  Its denominator counts literal address uses, so accessor-only files
  disappear instead of scoring as named. Include accessor uses or report
  converted files separately (`tools/ram_map.py`).

- **`portstyle.py`'s function count** misses one-line definitions and combines
  guards and entry wrappers with logic. Match those definitions and split the
  categories so the reported machine dependency identifies remaining work.

- **Scene buffers reached by address.** Name fixed script bytes,
  line-effect descriptors, race setup buffers and mirroring cells in the
  map; make caller-selected scaler rows, sprite lists, copies and script
  stores explicit resources. The title's tile list at `$7F:0200` aliases
  scaler output and event variables, so its ownership must be decided before
  splitting storage (`game/scene/`, `wram_*`; DISASSEMBLY.md, "The RAM map").

- **The unpacker and the original part on streams that aren't streams.**
  Resource numbers beyond the scene table's 73 entries differed in 30 of
  2,000 old fuzz states; valid resources agreed. Define and check the
  `unpack_stream` source/destination contract, especially a source in work
  RAM overwritten by its destination. The scene and unpacker comparisons
  have retired; replay the case as a coherent-state test rather than relying
  on the former registry entry. Catalogue membership alone does not prove
  that every stream is valid.

- **Separate the setups' mirroring time.** The race and ending arrangers
  (`$7E:3EB7`, `$7E:3ED3`) contain long loops. Measure their work separately
  in the native time model before changing the setup rates; the old timing
  exceptions no longer provide independent scene comparisons.

- **Scene review follow-ups:** separate the timing sites from the panorama
  layer data in `endings.c` (`load_the_panorama`'s `layers` table); represent
  the turning figures' character IDs as bytes in `turning.c` (`start_figures`); give `scene_data.h` decoded poses instead of reads by
  address; and separate script timing metadata from command
  semantics in `script.c` (the command table).

- **What scene's second review accepted as later work** (history/reimplementation-phase3a.md, "Phase
  3a: scene reworked to the review"). None changes behaviour; each is a
  reading the code should give and doesn't yet:
  - `scaler.c`: the carry into the leftover's measure is threaded through
    three functions (`scale_row`, `end_bitmap` and the entry's flags) where one
    named input would say it; `charge_block_move(bytes - 1)` needs its reason
    at the call (the original clears the first byte and block-moves it over the
    rest, so the move is a byte short of the fill); and `in_scaler_bank(at,
    offset)` has callers that pass 0 as the base and the address as the offset.
  - `circle.c`: the menus pass `mode` as an untyped byte (`scene_circle_draw`),
    which wants an `IrisShape` and a `bool leave_outside`; the `count` local
    exists only for the scaffolding that leaves the original's count cell.
  - One `scene_queue_sound` for the two copies of "into the sound command
    buffer, sent unless a song is on its way" (`frame.c`'s song and
    `script.c`'s `queue_sound`).
  - `frame.c` derives the controller's carry (its compare with the reset
    buttons) twice, at the pause sound's call and at the task lists'; once,
    named, would do.
  - `SCENE_VBLANK_TASKS` is the map's `scene_early_tasks`: one name for both.
  - `math.c`: `FoundDivisor.tried` is the search's leftover, not what it
    found; and `divide_8_8` computes where a value lies along a range, so
    `fraction_along`.
  - `race_matrix.c`'s generator keeps running accumulators where each row's
    offset could be computed from its index.
  - The bank `$FE` resources (above) wait for the platform.
- **`scene/scene_data.h` reads bank `$FE` by address** (`scene_data_byte`,
  which the resource copies use): the asset seam wants a named accessor for
  each resource the table at `$FE:0000` points to.
- **The scenes' text is read out of the unpacked program by address**: the
  staff roll's credits (`staff_roll.c`), and the attract sequence's pages of
  credits with their tables of where each page's names and jobs start
  (`credit_pages.c`, `wram_read8`/`wram_read16` on `$7E`). The asset seam wants
  a named accessor for each, as for the resources in bank `$FE`.
- **A script's task search reaches work RAM by address**
  (`scene/frame.c`, `scene_script_replace_task`, `word_past_the_list`): past
  its list the search reads and writes any word of `$7E`/`$7F` from
  `RAM_SCENE_TASKS`/`RAM_SCENE_EARLY_TASKS` through `wram_read16`/`wram_write16`,
  as the original's search does (mode 10's script clears a word of itself).
  Once the scene's storage leaves the RAM image this finds nothing; the stray
  wants a decision in game terms (which words a script's command can reach)
  before then.
- **The palace's turn writes its matrix by bare index** (`rising_palace.c`,
  `work_out_the_turn_task`: `set_scene_turn_matrix(g, 0..3, ...)`): the four
  entries want names (which is the cosine, which the sine and its negation),
  in the RAM map or an enum, as the turn's position and centre have theirs.
- **Share the curtain step's identity.** `CURTAIN_STEP` for `$C3:01E4` is
  defined in both `rising_palace.c` and `curtain.c`; one definition in
  `scene.h` would keep the timing and checked call sites together.
- **The credit pages read byte cells through word accessors**
  (`credit_pages.c`: `next_page`'s `scene_credits_page(g) & 0x00FF`,
  `write_the_jobs`'s `scene_sprite_offset_x(g) & 0xFF00`): narrow the
  declarations in `re/ram.toml` so the masks leave the port.
- **The RAM map can't say two variables alias.** `ram_map.py check` rejects
  overlap inside a subsystem, but the ROM overlaps: the map planes' last column
  word is the first plane's row word (`$7E:00E7`), which had to be modelled as
  two arrays of different lengths with the alias explained in prose. An
  `aliases` field would let an entry say what is true.
- CI: the macOS job costs 10x minutes on a private repo.
## State storage ownership

The remaining image-backed variables are measured by `tools/ram_map.py storage`;
the per-site and per-pair inventory is in [STATE.md](STATE.md). Before moving
another module, resolve its listed aliases and raw clients, including shared
initial contents on mode transitions. Do not give two struct fields one cell.

- **The generator, before larger slices** (the state-as-C review): host
  accessors for indexed variables drop `CT_ACCESSOR_CHECKS`' `CHECK_INDEX`
  (`tools/ram_map.py`, the host fallback); keep it before an indexed variable
  moves. The address projection finds a field by a linear scan per byte; give
  it indexed dispatch before bulk clears, saves and video reads cross many
  moved fields.
- **Overlapping variables need shared ownership.** 2,011 of the 2,263 overlap
  another; independent structs can't keep those relationships, so the next
  slices need an explicit owner and alias views per overlap, decided module by
  module before anything overlapping moves (the generator rejects it until then).

- **Core:** controller/clock and palette scratch overlap other uses; character
  records contain both whole-record and named-field views. Inventory tail
  accesses and save projection must keep a single owner. Sound's retired cells
  include unchecked tables and should be separated from live sound state.
- **Data:** decompressor destinations and its streaming WRAM port are dynamic;
  establish which output buffers are game state versus decoded resources before
  moving the eight otherwise non-overlapping variables.
- **Event:** story memory, event variables and field script storage overlap;
  replace arbitrary-address script operations with owned views where possible.
- **Field:** map/graphics/scratch reuse and interleaved row tables need owners;
  video queues, unpack destinations and unchecked object/table tails need views
  of those owners rather than independent storage.
- **Menu:** direct-page records and shared screen scratch need ownership across
  screens; inventory overruns and tilemap/DMA sources need explicit views.
- **Menu objects:** all 39 variables overlap another module; resolve object
  records, menu pages and overworld sharing before moving them.
- **Overworld:** object-mode sharing, map scratch, unchecked tails and video
  staging must be separated or assigned one owner.
- **Battle:** 909 variables overlap others; resolve combatant/scratch aliases,
  whole-record field views, 53 unchecked tables and graphics sources in slices.
- **Scene:** own/channel page instances, shared mode scratch, line-effect and
  upload buffers need ownership; retain initial values across mode changes.
- **Warp:** plane/light graphics and line-effect sources share scene memory;
  resolve those aliases and the unchecked table before moving the module.


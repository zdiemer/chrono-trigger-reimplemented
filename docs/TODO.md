# Open follow-ups

Loose ends the milestone docs record in passing, collected so they don't get
lost. Each line says where the detail lives. Milestones themselves are in
[APPROACH.md](APPROACH.md), [GENERATED.md](GENERATED.md),
[REIMPLEMENTATION.md](REIMPLEMENTATION.md), [MATCHING.md](MATCHING.md)
and [PLATFORM.md](PLATFORM.md); which of these to work on next, and why the
rest waits, is [PLAN.md](PLAN.md).

## Correctness and accuracy

- **Hand ports' time is an estimate**, so `--native on --generated on` drifts from
  the original (battles play out differently). That difference is accepted
  (REIMPLEMENTATION.md, "Can a hand port be exact?"); the timing gate bounds the errors
  (REIMPLEMENTATION.md, "The timing gate"). Left open there: 25 entries whose
  time follows a path inside one routine, or a loop of the port's own, are on
  `tools/timing_exceptions.txt` (the decompressor, the menu's text, glyphs and
  wave table, `field_rasterize_quad`, the field's sprite table, line-effect
  builders and map unpacker, whose rewrites no longer make the index steps
  the original's time follows, the field's object update, whose steps now
  run in its own call, the scene's circle, the battle's wave
  windows and HP gradient, the free-slot walk that starts a menu object, and
  the own loops of the effect slots, the object
  drawing and its underlays); a dispatcher's handlers are attributed to
  instead (REIMPLEMENTATION.md, "Dispatchers"), and the menu text's control codes would
  be too once their handlers are entries. Adding a movie or a port means
  recalibrating, since the drift limit holds only for the measured value.
  `--generated only` stays exact.
- **Fuzzing coverage of existing ports.** Fuzzing is now part of every port's
  definition of done (PORTING.md); ports registered before that without a
  recorded fuzz run should get one, subsystem by subsystem as Phase 3a
  rewrites it (PLAN.md): a rewritten port needs fuzzing anyway.
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
- **`battle_draw_objects` on unbounded counts.** Fuzzed with the part and
  sprite set counts unbounded (which the fuzzer can do now that it stops a
  port's own loop), 40 of 20000 states from a `gato-battle` state differ: in 28
  the port runs on past its budget where the original returns after about
  200000 accesses (by then the draw list position `$1DC3`, which the sprites
  overwrite, is far past the list, and the port meets no end entry), and in 12
  a byte of the drawing page's cells (`$1DC7`, the part count) or `$0049`
  differs. No movie reaches these; they are the port's to fix or to declare
  out of reach (`ct_fuzz --routine battle_draw_objects --range A=0000-00FF
  --db 7E`).
- **`$213F`'s bit 7 is the beam latch's flag in the runtime's PPU**
  (`runtime/snes/ppu.c`), where the hardware has the field's parity (and the
  latch's flag in bit 6). The scenes read it as the frame's parity
  (`tm_odd_frame`: the endings' slow credits, `$7E:9AB3`), so on the runtime
  they move on the frames after the random mixer latched the beam, not every
  other frame. Shadow mode compares the runtime with itself; the oracle's
  traces of the Beyond Time ending would show it.
- **The last of `$CF`**: `$CF:FAE2` (259 bytes) and `$CF:FD9E` (231) are the
  bank's unported runs (`python3 tools/burndown.py --uncovered CF`).

- **A refusal the tech sweep's route finds** (`tools/refusal_allowlist.txt`;
  REIMPLEMENTATION.md, "The fallback measure", "The last refusals in real play: the
  attract demo's states"). The library makes none now; the tech sweep's
  route, not yet in it, finds `script_return_from_call` (`$C1:56FB`), whose
  `command_ready` turns away the sweep's battle script 8 times from frame
  11015 on; the movie (`tools/movies/routes/tech_sweep.py`) waits on it.
  `python3 tools/fallbacks.py build/suite/fallbacks --refusals -v` prints it
  with its first states.
- **The tech sweep's fight never ends in the native shape.** The route
  (`tools/movies/routes/tech_sweep.py`, zophar chrono-trigger slot 1, the Lavos
  Core) reaches its outcome in the shipping mode, but `ct_drive_native_shape`
  ran it 400,000 frames without the fight ending: with the scaffolding
  compiled out, something in that battle stops the fight from finishing.
  Find what logic reads a cell only scaffolding writes there
  (`ct_dev --scaffold-reads original`, the route's frames) and make the two
  builds agree. The route runs by hand (`tools/suite.py`'s `HAND_RUN_ROUTES`).
- **An ending's menu script sets the sprites' sizes by `$2101`.** The menus'
  object scripts' byte command `$13` names its cell by address, and an ending
  writes the sprites' size and tile-place register through it; the port hands
  the write to the SNES escape `vid_snes_write_register`
  (`menu/script_bridge.c`, `set_console_byte`), which only the SNES backend
  has. A build without it needs the script's write as what it means: decode
  the scripts as an asset so a command naming `$2101` becomes a sprite
  setting (`VidObjConfig`) the service takes, and drop the escape.
- **What the original's code reads of scaffolding** (REIMPLEMENTATION.md, "The
  fallback measure"). It broke the native shape's three whole routes until
  the targeting was ported; what it reads now (101 byte and instruction
  pairs over the library when the measure came in, `tools/effects_check.py --originals`
  on the fallbacks check's logs, `build/suite/native-shape/originals.txt`)
  hasn't broken an outcome yet, and is the list to decide before the
  scaffolding goes: the menus' `$007A` (`menu_open`'s, read at `$C2:CF0A`
  on), the equipment screens' `$0022`-`$0027` and `$0DAB`, the battle's
  `$0084`, `$0092` and the enemies' `$A020`-`$A026`, and a block copy at
  `$C0:0924` that reads the field's cells whole.

## Phase 3a work in flight

- **What `tools/portsmell.py` found when it was written** (its baseline,
  `tools/portsmell_baseline.txt`, is the list; each line leaves it as the work
  lands):
  - 20 `source` lines in `re/ram.toml` cite files that no longer exist
    (`game/battle/stats.c`, `math.c`, `gauges.c`): rubric item 25, a map fix.
  - 17 call-outs reach one of our own registered ports through the machine
    (`game/menu/epoch.c` 7, `game/menu/display.c` 4, `game/battle/display.c` 2,
    one each in `battle/nmi.c`, `field/display.c`, `scene/boot.c`,
    `scene/frame.c`): rubric items 22 and 61, each to become a C call behind a
    helper that tests the guard (`event_call_bridge`'s shape).
  - 4 enums nothing takes as a type (`MenuPage`, `AssignedSet`, `MenuSound`,
    `DialStep`): rubric item 33, type the parameters that carry them.
  - 2 comments that admit they don't know what they describe
    (`game/battle/frame.h`, `game/battle/screen_gradient.c`): rubric item 17.
  The four bridges' `s->cpu.pc` comparisons are on the baseline with a reason:
  asking whether a call-out returned to its site is not entry identity.
  - 9 flag notes inside a loop or a game branch (rubric items 93, 105):
    battle's `effect_amounts.c`, `enemy_turn.c`, `member_stats.c`,
    `member_turn.c`, `rewards_techs.c`, `tech_records.c`, event's
    `movement.c`, menu's `save_screen.c`, scene's `frame.c`; each to become
    one leaves derivation from the final state. Five more are on the baseline
    with a reason (a wrapper's entry state for a call-out, a shared wrapper's
    exit, the scene interpreter's per-command derivation).
  - 12 return frames pushed by hand outside a bridge (rubric items 85, 101) in
    `attract/main_loop.c`, battle's `animation.c`, `enemy_turn.c`,
    `main_loop.c`, `screen_beam.c`, `core/input.c`, `menu/cursor.c` (3) and
    scene's `frame.c` (2) and `script.c`: register the inlined subroutine as
    its own entry, or port the call-out that needs the frame.

- **Scene's follow-ups from its targeted re-review** (merged with these
  accepted; `script.c`, `sprites.c` and `title.c` rated mostly):
  - `scene_script_wait_for_sound` (`script.c:725-748`) keeps the wait's
    decision in the entry: split out `music_wait_over(g, ch)`;
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
  - `sound_driver.c` still reads as translation, and is not to be rewritten: it
    is the original driver's upload protocol, which the C driver doesn't use
    (it takes the boot and the commands as calls) and which goes with the
    original driver when the gate retires for sound (PLAN.md, Phase 4a);
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
  - The original's routines the battle still calls out to: the battle steps
    (`$C1:0003`) and the rewards' gold `$C1:D0A2` and messages `$FD:AD17`;
    from a member's chosen action carried out (history/reimplementation-chunks.md, "Phase 3, battle
    chunk 5"), the messages `$CD:0021`; from an enemy's turn (history/reimplementation-waves.md,
    "Phase 3, wave 2: the enemies' minds"), an enemy's
    coming in `$FD:B438`, the calling in
    `$FD:A990`, the message `$CD:0033` and the action's showing
    `$C1:BFA4`/`$C1:EC7F`; what an effect's working out turns away to the
    original (history/reimplementation-chunks.md, "Phase 3, battle chunk 4: the party's working
    out"): a hit that steals as well (no record has one); the effect's start a status's timer asks for
    (`$C1:EC7F`); the
    pose's setup `$C1:308C`, and an animation that names no movement
    (`$C1:3760`'s table past its nine rows, no animation the battle gives);
    the decompressor from the effect graphics loader; `$CD:0027`/`$002D`/`$0030`
    from the menu window; from a member that comes back once (history/reimplementation-chunks.md, "Phase 3,
    battle: the leftovers"), the showing's step `$C1:BFA4`/`$C1:EC7F` and
    the message `$CD:0021`; from the battle's start and its return to the
    field, the battle screen's setup `$CD:0000` (`$D1:F1B0`), the display's
    closing `$CD:0006`, and from the setup's
    end the party's poses `$C1:35DD`; the wait tail's callees from the frame
    work, the battlefield put back `$CD:0453` and an effect's graphics
    loaded `$D1:ECF3`; the menu's turn the frame work's wait gives
    (`$C1:0012`); and from the screen's start (history/reimplementation-chunks.md, "Phase 3, battle:
    cuts, bands and the screen's start"), the start settings' copy
    `$CD:0C9D` and the battle music's start `$C0:0008`.
  - What the screen script's rows still turn away to the original
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
  - `$CE:E1C7`-`$CE:E292`, an unrolled byte copy next to the ripple's step,
    is code nothing the analysis knows reaches (`re/hints.toml` keeps it
    `unreached`); the burndown counts it as bank `$CE`'s 5% left.
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
    run as C, lifting by `POINT_JUMP_LIFT`, only while `$90`
    (`battle_jump_lift`), a work cell the original shares, holds the 6 the
    animation slots' loop leaves there each frame; a jump given its own lift or
    a walk's search earlier in the frame overwrites it, and the original's jump
    then lifts by that (no movie has one). The throw's last step `$C1:7364` is
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
  - a divergence the rewrite accepts, on inputs no game state makes: a
    string's control code past `$0C`, or one included deeper than four, draws
    nothing, where the original jumps or recurses (`menu_draw_text`'s fuzz
    mismatches in PORTING.md);
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
    - the dial's and the map view's screen setup runs in the `Snes *` entry,
      about 25 shadow writes in the original's order: split the runs without
      call-outs into `GameState`/`Platform` functions;
    - `draw_map_tile`/`draw_map_picture` return the last group only for the
      leftovers;
    - `upload_gate_rows` accepts a count of 0 and a table anywhere; the guard
      should refuse what the game never passes;
    - `sine()` is written three times (`epoch.c`, `top_layer.c`, `wave.c`),
      and `OBJECTS_HELD`/`SCREEN_OFF` twice;
    - the End of Time's gate screen (`menu_start_gate_screen`) is not an
      Epoch screen and wants its own file;
    - sign-bit compares, an uncommented `_unchecked` read in `group_pixel`,
      and the numbered entries `menu_object_window1_lines_2`/`_3`.

- **What event's and field's combined pass left** (history/reimplementation-phase3a.md, "Phase 3a: event
  onto the field's objects"):
  - a location is still decoded from its two words in the operation
    (`location_of` in `event/locations.c`): the rows decode an operand into
    one number, and a `Location` wants the decoder to build a struct;
  - `nmi.c`'s `uploads[]` repeats `display.c`'s edge runs' routines:
    `tools/dontcare_check.py` reads a call-out's site and routine only from the
    file that makes the call, so sharing the table needs the check to follow a
    table in a header;
  - the staging buffers the field's transfers read are addresses in bank
    `$7E` (`display.c`'s edge runs, the VRAM queue), which a transfer service
    that takes a named buffer would replace (PfData's host pointer, which the
    SNES backend doesn't take yet);
  - computing through the work RAM port and raw addresses: `layers.c` reads
    the map data as a stream through the port, `sprites.c` its records by
    address, `graphics.c` its buffer, `display.c` its staging buffers. Each
    needs the data's entries in the RAM map first (a stream's layout is the
    map data's format, MATCHING.md), which is its own pass;
  - a screen or camera module: the screen's tile and fine position is worked
    out in three of the field's files (`scroll.c`, `object_view.c`,
    `line_effects.c`), and event's "well on screen" is a fourth reader; one
    module that owns where the camera is would give them one vocabulary, and
    is a design of its own;
  - naming and organisation the reviews asked for, none of which changes
    behaviour: `scroll.c` is the automatic scroll (`auto_scroll.c`),
    `display.c` and `nmi.c` split by what the vblank sends rather than by
    where the original's code sat, a dialog-window type for the band the line
    effects build, and `field.h`'s header saying what the file is;
  - wrappers that grew logic: `line_effects.c`'s `leaves_after_send` works out
    which of three paths the send took to rebuild its registers; a
    derivation per path, beside the others (`field/leaves.c`), would keep
    the wrapper thin;
  - `window_shape.c`'s `shape_leaves` records the last sloped edge while it
    draws; its width and height follow from the last edge the shape leaves,
    and only its error needs the record;
  - event's entries named by meaning: `face_0`..`face_3` are `face_up` and
    the rest, `loop_animation_0`/`_1` the idle and walking loops,
    `party_routine_*` what the menus' routines do, `*_1e` what
    `field_setting_1e` is once that is known;
  - `move_to_tile`'s branches follow the original's order more than the
    game's, and its sliding tail repeats `arrive`'s;
  - the tint's colour kept as the hardware's bytes (`screen.c`);
  - the bit masks read from the ROM's table by a bit number (`event_data.h`),
    which a guard of `n < 8` would let the port compute;
  - types: a `Comparison` for the conditions' operator byte, the calls'
    urgency as an enum, and the flags still carried as bytes;
  - `sound.c`'s four bridges repeat one shape (keep the offset, leave the
    parse, call the driver), which one helper would say once;
  - `calls.c`'s `place_found` is a lookup with a scaffolding side effect,
    which the derivations could take over.
- **Shadow mode pairs a bridge's calls in order** (a tooling gap, not a port
  fix). A bridge that may or may not call the original's code has to predict,
  in its guard, whether it will, because shadow mode pairs the port's call-outs
  with the original's in order: the field's `field_view_changes`,
  `every_draw_is_own` and `records_work_is_own` are those predictions
  (review-port item 29, guards that duplicate the logic). Pairing call-outs by
  site and routine rather than by order would let the guards go.
- **Coverage that field's rewrite thinned.** Inlining the object update's
  steps into their callers' time left more of their calls interrupted, and
  shadow mode compares none of those: `field_update_object_sprites` went from 78
  interrupted calls to 855, `field_draw_object_sprites` from 837 to 1713,
  `field_build_sprite_table` from 267 to 336 and `field_update_objects` from 6
  to 17. The fuzzer covers what shadow mode skips, except that
  `field_build_sprite_table` compares 0 fuzz states on either side (its draws run
  past the call-out budget), so it has shadow mode's uninterrupted calls and
  nothing else. A fuzz setup that bounds its draws would close that; the
  field's return from a battle, which calls it, is fuzzed only up to that call
  for the same reason (what follows it is its own entry,
  `field_hold_party_if_defeated`).

- **What only the original's memory has, past the object tables.** The
  stray accesses are explicit and marked now (history/reimplementation-phase3a.md, "Phase 3a: event onto
  the field's objects"), and a build without the original's memory has to
  decide each: the sprite table's high part running on past its end
  (`sprites.c`, by address through `game/wram.h`), the fifth sprite's Y above
  the screen, the openings' word clear spilling into the next slot (the first
  object's falling flag, past the last), and the row removal's in-view
  write. Three commands still take a party member's object without checking
  (`$24`, `$A9`, and `$07` while it waits), and their guards leave an empty
  place to the original, where it reads the slot past the tables; the game
  makes none of those calls over the library. The moves, following and
  noting a member's tile ask for the member as `Someone` (`event/party.h`),
  which has nobody in an empty place, and these would do the same (history/reimplementation-chunks.md,
  "The absent party member and the object off the map").
  The party's place taken as the leader's with no leader on the map (a
  save's load, the attract demo's scenes, until the map's objects start), for
  a menu (`$C0:011B`) or a location change (`$C0:0C8F`), reads the tile and
  facing of the first slot past the tables, as the original does, through
  the unchecked accessors (`field/location.c`, `field_leaders_place`): the
  map's start then puts the party where the location says, so a build without
  the original's memory can take the location's own place there instead.
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
  - the rewards' messages (`$FD:AD17`) are still the original's: each waits
    for the player through `$CD:0021`, so their port waits on the messages'
    own (the window's text and the wait for a button);
  - the tech points' count (`rewards_techs.c`, `rewards_count_tech_points`)
    pays the battle's points again towards a tech whose cost the points left
    over from the last one don't reach, so a member can learn more than the
    points pay for: a native build decides whether to keep the original's
    count;
  - a level a character in reserve gains is noted in bit 7 of the byte after
    the members' three, which is the first member's techs' byte (no message
    tests that bit there), and counted in the byte after the members' counts,
    whose carry reaches the battle's reaction entry (`rewards_levels.c`,
    `count_level_gained`): name the reserve's bytes when `$FD:AD17` is ported,
    or drop them if the messages never read them;
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

- **Nobody leading** (game/field/object.h, `FieldLeader`). With the
  leader's place empty, the original walks the slot its marker names, past
  the object tables: where that slot stands, its tile and its place on the
  screen come from the tables after the objects', and the ground it steps
  onto lays its priorities on the bytes after the priorities'. The screen
  follows it, which shows (the shipping mode's late-omen-terramutant reaches
  it at frame 14887 with a direction held). The confirm button searches
  before that slot, facing the way the byte after the facings' says, and
  looks for a chest on the tiles before it, off the map (the walkable
  table's bytes past its rows, up into bank `$7F`; `event/chests.c`,
  `walkable_past_the_map`): the endings' fields press it so. A native build
  has nobody to walk or to look before: decide what the screen and the press
  do then, and drop the slot's reads and writes with the tables' native
  storage.

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
  "Phase 3, battle: an action shown"): the steps' dispatch (`$C1:0003`) and
  the showing's stub (`$C1:BFA4`), whose callers' declarations rest on the
  library's evidence for the overflow they hand the steps, so a port of the
  dispatch moves that evidence with it; the menu's turn the frame work's wait
  for the vblank handler gives (`$C1:0012`, `$C1:10E3`), after which every
  register is live; and the frame waits (`$C1:007E`), which wait on the
  vblank handler and go with the timing service (PLAN.md, "Timing is
  decided"). Poses 1, 10 and 11 have looks (`pose_looks.c`) no status gives.
- **What the battlefield's change left** (REIMPLEMENTATION.md, "Phase 3, battle: the
  battlefield changed and an effect's rows of its tiles"): the rest of the original
  that loads a battlefield, the screen effects' song and the HP gradient's
  (`$CD:0D62`, `$CD:0D93`-`$CD:0DF9`, `$CD:0E05`) and the battlefield put
  back (`$CD:0453`, with `$CD:0E23`-`$CD:0E61`), which call the ports; and
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
    half and the parts' largest counts;
  - the event engine reads the script past its buffer (`$C0:5A93`,
    `$C0:58DE`): the opcode byte before the command's guard, and an opening's
    end byte. Treating every offset past the buffer as off the script (not only
    `$FFFF`) is right for the turns, but the openings' hand-over at `$C0:5900`
    then mismatches in fuzzing (254 of 333 states), so the openings' path
    off the script needs fixing first.
- **The shadow movies ran 20% longer with the accessor checks in two suite
  runs** (393 s against 317 s, and 398 s against 327 s, `-j 5`), but a movie
  run alone or four at a time, the builds alternating, shows 1% or less
  (REIMPLEMENTATION.md, "The accessor bounds checks"). Find what differs in the suite's
  runs before reading the gap as the checks' cost.

- **What the jet bike race's review left** (none blocking after its fixes):
  `race_rules.c`'s `drive_johnny` reads the lane through `race_lane_high`
  (the lane's high byte with the byte after it as a word, race.h) where the
  lane would say it; `sprites.c` exports `scene_clipped_list_leaves_a`, a
  register derivation, from a logic file; `hw_divide_by_word`'s contract
  describes the word store spilling into the next register (prefer
  `hw_divide_by` plus a marked note of that byte); `math.c`, `sprites.c`,
  `race_camera.c` and `race_matrix.c` rated "mostly". Fuzz states for the
  scene program need `--keep 7E3000-7EC994` (the program's own code), which
  older entries' arguments stop short of at `B5FF`.

- **What the staff roll's review left** (merged with the telepod ending;
  none blocking): `staff_roll.c` tables origin tags with `vram_site` inside
  the game record (a parallel site table); sets the screen with nine
  `vid_set_*` calls in the original's order where one `VidScreen` through
  `vid_apply_screen` would do; `copies_bank` is always the lower bank once
  the destination's bank is cleared; the high-half fill takes its byte from
  the program's code (`$7E:9E7A`) without naming the priority byte; `+1`
  where guarded cells would make `true`; 17 where `PICTURE_PALETTE + 1` is
  meant; a palette copy by address beside accessor writes to the same
  palette. `script.c` maps an unlisted nibble silently to the byte store's
  handler (a per-row column or an assertion), declares nothing dead at the
  hand-over stop, and with the command limit gone a script that never ends
  its turn spins the host's frame in C. A half-word VRAM fill
  (`vid_send_vram_fill` taking a `VidVramHalf`) would retire the staff
  roll's two `vid_snes_copy` escapes and the title's `$7E:5AEB`; until then
  PLATFORM.md's as-built escapes should list them. `tests/test_burndown.py`
  has no case for `PF_CALL_DC` call-outs.

- **What the race's second review left** (none blocking after its fixes):
  `perspective.c`'s `start_lines` writes the line step to a cell the builder
  reads back (return it), and `PerspectiveStart` keeps the patched call's ROM
  addresses as its values (decode them to a scene kind once); `race.c`'s
  graphics table keeps the `vram_site` origin tags in a column, with 0 also
  meaning "carry on" (a parallel table, and "carry on" its own field), and
  `crono_blocks` gives raw addresses where a picture, row and column would do;
  `race.c`'s `on_page` duplicates `bridge.c`'s; `race_matrix.c` writes
  `set_scene_scaler_request_state` plainly though nothing reads it
  (ORIGINAL_EFFECT); `math.c`'s `scene_multiply16` and `scene_divide_8_8_by`
  pass operands and results through `scene_call` cells between C functions;
  `race_rules.c`'s `with_low_byte` and `& 0xFF` want the map to narrow those
  cells.

- **The time warp's flicker reads past its table.** `warp.c` takes the
  flicker's row 256 x `past` bytes beyond the flicker table
  (`warp_data.h`), `past` being the bottom line's scale left in A's high
  byte, as the original does. It is deliberate and matches, but it reads
  whatever ROM follows the table: a build that loads the table from an
  asset pack has to carry those bytes too, or the warp has to be shown
  never to reach past the table (fuzz the scale's range) and the read
  clamped.
- **What the time warp's review left** (none rated translation):
  `WarpKind` carries ROM columns (`frame_entry`, `window_site`) beside the
  kinds' length and flicker, used as `PF_AT(...)` and `charge_as(...)`
  (a parallel dispatch table); `warp_build_circle` returns its step count
  only for the timing charge, and `HalfWidths`' `left_on`, `area` and `run`
  exist only for scaffolding; `circle.c`'s `h->area & NEGATIVE` is a sign
  test (`(int16_t)area < 0`), and its comment calls the fraction in
  `width[line+1]` scaffolding though the logic's own `h->width` may read it
  on the last line (check); `picture.c`'s callers pass origin tags into
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
  `set_scene_race_picture`, an accessor named for the race;
  `attract/main_loop.c` picks between two calls that differ only in
  address with an if/else. In `tools/timing_exceptions.txt`,
  `scene_ending_set_up`'s reason names constant work and the unpacker but
  not mode 10's mosaic, the path its time follows, and
  `scene_ending_load_picture` gives the ground variant's "at most 542602",
  where its own mechanism is the interrupt splitting a call of fixed work:
  check both against the gate's output.

- **What the world map's setup review left** (none rated translation; the
  `menu_epoch_build_map` guard is a real precondition and the main loop's
  call-out unreachable): `overworld/setup.c`'s loaders take the ROM call
  site (`0xC2280C`...) so the machine's unpacker can rebuild the original's
  frame, threading the call graph through the logic (give the unpacker a
  game-state form); `overworld_load_the_map_tiles` is called without
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
  accessors in `frame_data.h` take a built bank-and-place address, a byte
  offset into the fades and the descriptors' `0x8000` (take the set, strip,
  tile and fade kind and return records); the screen effects' settings
  (`$53`-`$61`, `battle_screen_effect_settings`) are written through the
  scaffolding writers `screen_leave_*` under `LEFT_*` names (they are game
  state the scripts read: write them through the settings, named for what
  the effect takes); `MODE_1_BG3_FRONT_16x16`, `BG3_MAP_AT_7C00` and the
  hand-packed tile entries are register encodings (the video service's
  terms), and `BG3_MAP_AT_7C00` repeats `MAP_VRAM`; `flip_tile`'s result and
  `MapBuilder`'s `last_*`/`built_any` exist only for leftover cells;
  `screen_script.c` clears the channels' state with `wram_fill16` at an
  address (name the block in the map).

- **What the attract refusals' review left** (none rated translation):
  `race_projection.c`'s `+ (gap <= SCREEN_LINES)` is a compare's carry
  (write the game's test, the bike at the camera with the gap wrapped), and
  `horizon_gap`'s `- !wrapped` carries an earlier sum's carry with a comment
  on the mechanics rather than what the player sees; its
  `keep_divisor(g, last_divisor(g))` writes back what the cell holds (drop
  it); `perspective.c`'s line-above-the-screen cells are written plainly
  though nothing reads them (ORIGINAL_EFFECT, or name the reader), and the
  two in `$7F` have no `reuses`; `attract/main_loop.c` runs the registered
  `scene_race_scale_pictures` through `m_jsr` (confirm it must, item 102) and
  describes the scene program's main loop from `game/attract/`; `native.c`'s
  entries at one address are sorted by `qsort`, which isn't stable, so two
  hand entries with overlapping guards would run in a library-picked order
  (break ties by table index, or assert at start that guards at one address
  don't overlap).

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
  rebuilt only to be decoded (also `display.c`); `epoch.c` writes
  `(location_flags & ~0x03) | facing` three times, leaves the
  `EPOCH_ARRIVALS` locations and rows unnamed, and `keep_a_stray_spot` should
  name the writer that resets the spot; `DACTYL_MAP`/`LOCATION_NUMBER` are
  defined in `vehicles.c` and `epoch.c`, and `over_the_dactyl` repeats
  `dactyl_on_this_map`; `vehicles.c` writes a signed-word test out by hand;
  the equipment guard `menu_list_what_fits_can_be_made` works out "can use"
  itself, and `power` starts at the slot's first item only for a leftover;
  `menu_data.h`'s new slot accessors split a comment from its function;
  `music.c`'s `overworld_play_the_song_in_view` sets the request's fields one
  by one past `ask_for`; `map_scenes.c` sets the view to 256x224 itself
  because the platform has no screen-size call (a platform gap).

- **What the overworld on foot's review left** (none rated translation):
  the leader's
  frames that build a map strip are off by 7,000-14,000 clocks (the scroll is
  timed as itself, at its own 1,445) -- charge the scroll per strip built (its
  two timing exceptions went with the vblank limits' change to stretches
  within one vblank, REIMPLEMENTATION.md, "The timing gate"); `followers.c`'s route is a packed byte and
  `walls_between` returns a bare byte (a `Route` type), `place & 1` stands
  for the second member, scaffolding is written on every try along the
  trail, and `set_off` places the walker another way than the leader;
  the map's size is written in five places (from `MENU_MAP_WIDTH/HEIGHT`),
  and native storage must decide what the wall search's read past the map
  returns; `main_loop.c`'s `OVERWORLD_ASIDE` lists raw work-RAM addresses
  (`0x7EB000` unexplained), story flags are read by index and bit, and
  `enter_the_arm` sets A and X with no reader named; event cells written as
  `1`; `walk.c` decides the ground news twice; `sign.c` patches a sprite
  script at an offset; `music.c` has a bare `3`. Fallbacks still to retire:
  the main loop's
  call-outs to the frame waits, the uploads' reset, the map loader
  (`$C2:2C93`) and the screen setup; check that no map loads an empty list
  and that a busy frame never runs the upload queue short (two guards
  refuse those). `entries.c`'s `EVENT_SCRIPTS = 0x7F0000` repeats
  `party.c`'s `EVENT_SCRIPTS_BANK` under another name.

- **What the party exchange's review left** (merged with the exchange and
  the gate's song; none rated translation): the emptied place's dimming
  halves fifteen words past the palette as scaffolding, which the native
  build skips; that path runs every time the panel shows with an empty
  active row, so show the skip is invisible or keep the halving, and the
  TODO's "field's tables" is wrong (the words land on `battle_*` cells at
  `$7E:A5A2`-`A6A0`); `exchange_bridge.c`'s guards redefine `NO_ONE`,
  `STAYS` and `NOBODY_CHOSEN` and decode the rows themselves (export the
  decoders from `exchange.h`), and test a party byte with the row's empty
  bit (`party_place_is_empty`); the same "free" test is written two ways in
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
  offset; `description.c`'s `item_row_fits` checks nothing; the effects
  baseline's `menu_shop_member_cursor` line carries the save slot's reason.

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
  `leave_use_found` re-runs `use_of` and the slide leave repeats
  `slide_brings_a_row` with `0xF0`; `hide_the_scroll_bar_and_cursor` takes
  two cells down as one word with a borrow; `hide_the_scroll_bar`, the scroll bar's thumb at a row (`thumb_at_row`,
  also the techs screen's) and the equipped-slot layout `0x27` are copied
  between files; also the scroll bar's hidden flag
  set to `FIRST_MEMBER_PLACE`, `menu_leave_load(s, 0x62/0x42/0x2E)` literals,
  `& 0xFF00` merges, `hw_set_dividend` and the list-rows scratch in logic
  files, `menu_items_after_*` named by position, `put_bands_at_rest` taking
  byte offsets, `menu_items_used_up`'s ignored result, and the cursor's item
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
  `screen_corner` is in `battle_start.c` and `battle_return.c`, and
  `refresh()` in `requests.c` and `screen_effects.c`; BG3's tiles at VRAM
  `0x5000` and the bank `$7F` buffers (`0x7F5080`, `0x7F6000`, `0x7F7000`)
  are literals, and `main_loop.c`'s `power_on_clears` uses raw ranges;
  `location.c` tests `field_map_has_no_bg3_set(g) == 0` where a bool query
  wants to sit beside `field_map_names_a_bg3_set`; `palettes.c`'s
  `SlotLooked` shapes only scaffolding, and `slot << 1`/`>> 1` is an
  unnamed palette encoding; `field_kept_party_script_tiles` packs `y<<8|x` by
  hand; `battle_return.c` names `0xC00715` bare; `console_install_*_handler`
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
  scaffolding; the bytes of `field_line_screens`, `field_line_bg_maps` and
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
  overwrites the third record (`$C0:463D`); `characters.c`'s header calls the
  unpacker the original's code (it is `data/decompress.c`), and its
  `leave_place_registers` works Y out inline unlike `field/leaves.c`;
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
- **`scene_frame_work`'s fuzz states rarely reach its task lists' call-outs.**
  Random task lists at `$0940`/`$0920` send the call-outs past the budget in
  about half the states, and in 20000 the rest scramble only a handful of
  call-outs (`ct_fuzz --poison-dc` reports how many). The two calls are proven
  by the strict solve, so nothing rests on it, but a constraint that keeps the
  lists' entries inside the program's tasks would fuzz what the tasks do with
  the registers the port passes.
- **What Phase 3's first `$C0` chunk found the tools lacking**, still open:
  the event engine takes a second table row for an opcode silently, the last
  one winning, where it could warn; and proving V dead at the command level
  (following the field loop's `JML`s) would remove most of the Y/V
  derivations the state routines' ports carry.
- **Reads of scaffolding the effects check holds in its baseline**
  (`tools/effects_baseline.txt`; REIMPLEMENTATION.md, "The tooling pass after Phase 3's
  first chunks"). Each is a value the native build reads stale unless shown
  otherwise; answering one deletes its line. What the library's shipping-mode
  runs catch (`ct_dev --native on --scaffold-reads`), by subsystem:
  - field and event: `field_current_object`'s high byte (`$016E`), which only
    scaffolding clears (`event/engine.c`'s `leave_current_as_a_word`,
    `field/sprites.c`'s `leave_current_object_as_byte`), is read as part of the
    word by `object_current_is_one` and `object_make_current`, in
    `event_run_init_scripts`, `event_run_object_script`, `field_update_objects`
    and `field_build_sprite_table`: in the native build it is whatever last
    wrote the word;
  - menus: `menu_scratch` (`$0001`), read by `menu_draw_member_portrait`
    where its own scaffolding wrote it last; `menu_object_wave_hdma` reads
    `menu_wave_phase` (`$01E2`) once after `event_run_object_script`'s
    scaffolding wrote that byte (`menus-end-of-time`), which may be a field
    cell over the same byte rather than the menus' own.
  The source check's findings on main are broader (battle 87, field 29,
  event 3, menu 103, scene 15, core 1, by element, after the battle's and the
  menus' first chunks; `python3 tools/effects_check.py`): most
  are the entry wrappers reading the operands the original's callers hand
  them, which a line with that reason answers, but the `[only]` ones in logic
  (`python3 tools/effects_check.py | grep only`) are where to look first.
- **The run check doesn't mark event's parallel table.** `event/leaves.c`'s
  runner is `#if ORIGINAL_EFFECTS` code around the operation, not an
  `ORIGINAL_EFFECT`, so `--scaffold-reads` counts its writes as the logic's;
  the source check sees them. Evaluating the runner's before and after parts
  inside an `ORIGINAL_EFFECT` (or bumping `original_effect_depth` around
  them) would let the runs see them too.
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
- **"No game state produces it", checked by a tool.** Rubric item 34 says to
  guard inputs the game never produces instead of emulating them, and each
  such claim has to be checked. Scene's rework checked each one by adding the
  guard and diffing per-entry shadow totals against main. That showed the
  multiply's carry-in is reached 32,936 times (848 on every title screen), so
  the reviewer's suggested guard would have been wrong. A tool that takes a
  guard condition, runs the library, and reports how many real calls it would
  refuse, by entry, would make this a command instead of a gate run.

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
  `DC_AL` (`could add`), `battle_step_effect_slots` still carrying the handlers'
  A through its loop among them, and entries whose sets a stop kept small
  (`battle_update_combatant`, `event_run_object_script`, `menu_run_objects`,
  `field_object_in_the_way`, the event dialog and condition commands).
- **Two answers to "what is dead after this returns", and only one is
  authoritative.** For `$C2:0568`, `disasm/out/liveness.json` says
  `dead_after_return: X N Z`, while the draft's suggested `.dont_care` was
  `A X Y N V Z C` and `tools/dontcare_check.py` -- the gate -- accepted it. They
  are answering different questions: the JSON's `after_return` adds what an
  escaped return may read through a caller's frame, the draft's
  `ret_live_mask` (in `tools/generate/show.py`, not in `liveness.py`) asks
  `live_after_return` at the entry's own contexts and falls back to
  `after_return` only where that is unknown. Both are defensible and neither
  says so, which cost the map chunk time and left it reproducing registers it
  could have declared dead. Moving `ret_live_mask` into `liveness.py`, emitting
  it in the JSON beside the other, and naming in each docstring which question
  it answers would settle it once.
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
  - the main loop's arms are compared by shadow mode alone: fuzzing can't go
    past the frame's wait (`$C2:0346` spins on the vblank count, which no
    fuzzed state changes). No movie takes an exit to an event
    (`$C2:2417`-`$C2:244F`) or opens the party's exchange from the map
    (mode 9, the `menus-end-of-time` exchange is the gate's); nor walks
    from one region into another (`$C2:3011`-`$C2:309D`, the music's fade
    and the new song), nor clears the sign from the menus (`$C2:5711`).

- **What the vehicles and the screen's objects left** (history/reimplementation-chunks.md, "Phase 3,
  menus and overworld: the vehicles, the screen's objects and the calls made
  where they go"):
  - the dactyl's flight (`$C2:4CDA`-`$C2:5627`, about 2.4 KiB) is still the
    original's; `field-65m-bc-dactyl.ctm` (route `dactyl_65m_bc`) boards one at
    Tyrano Lair, flies a short way and lands, so it is ready to be ported and
    checked;
  - the objects' loop (`menu/objects.c`) and the scripts' call command
    (`menu/script_bridge.c`) still call a handler or a named routine through
    the machine, at its own address, so that each keeps its shadow
    comparison and its calibrated time: the native program needs a C table of
    the handlers and of the routines the scripts name, each row the
    operation's own (`NATIVE_TIME_AS`), with what nothing ports handed to the
    original;
  - what the scenes' scripts call and nothing ports yet: `$C6:E241` and
    `$C6:E27E`, the endings' scenes' line effects, which call the scene
    engine's `$C3:000E` (unported) and program a line channel the line
    service would take;
  - no movie flies the Epoch over the Black Omen or enters it
    (`$C2:4510`-`$C2:452F`, `$C2:4A0A`-`$C2:4A19`), takes off at the story's
    point `$D3` or boards at `$D2`, or sets off for `$1F2` before the Epoch
    has its wings (the stray spot it keeps there, `keep_a_stray_spot`): those
    paths are fuzzed only;
  - the Epoch's second sprite is fuzzed on few states (its guard asks every
    animation it may be given to be one the language reads, which random
    sprite scripts rarely are): a variant with the slot's scripts kept would
    cover it better.

- **What the world map's set up left** (REIMPLEMENTATION.md, "Phase 3, menus and
  overworld: the world map set up, its scenes' calls and the frame's
  input"):
  - the reference play runs 5.0 KiB of bank `$C2` with no call on record
    (`C2:----`): the menus' screens' code after a task's yield (`$C2:821E`),
    resumed on the task's own stack, which the fallback measure can't file
    under the port that stopped at the yield. Ports whose work spans a yield
    (or the measure following a task's resumption to the port that yielded)
    would show how much of it is really the original's;
  - the scrolling lists' cursor kept within the rows (`$C2:EA53`) and their
    frame laid out (`$C2:EA81`, the divider's quotient the scroll bar's
    step), which the equipment, items, techs and shop screens' bridges call
    out to, and the equipment screen's preview (`$C2:A5C3`, `$C2:A5E7`);
  - the world map's loads reach the unpacker through the machine
    (`overworld/setup.c`'s `Unpacker`): a C unpacker taking a stream and a
    destination would make them plain calls (the event commands' TODO wants
    the same);
  - `overworld_back_from_the_menus` is fuzzed up to the map's reload, which
    random states make through the original's call (a map the loader can't
    take); the reload itself is fuzzed alone (`overworld_load_the_map_again`)
    and checked in shadow mode (7 calls).

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
  - the save screen's own opening and frame (`$C2:D4F8`, `$C2:D506`) are
    reached by no movie (the library saves from the main menu's panel):
    fuzzing is their check, and their `cost_milli` is the placeholder;
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
  - the equipment screen's preview of an item's figures (`$C2:A5C3`,
    `$C2:A5E7`) stays the original's: the P it saves (under its calls to the
    figures, `$C2:916E`, and the description) is what the list's move
    (`$C2:9F05`) leaves at its return, which that port doesn't work out on
    its paths; what its steps call out to beyond it is still the original's
    too: the list of what fits (`$C2:8953`), the lists' sliding (`$C2:EA53`,
    `$C2:EB9B`, `$C2:EB1F`), the low-HP marks (`$C2:813C`), the tasks' stop
    (`$C2:8255`) and the main menu (`$C2:9A98`);
  - the member slide's start (`$C2:9D5B`) and the member list's scroll bar
    (`$C2:A170`) stay the original's: each saves P after a count of its own
    (the new first row, the row and scroll's sum) and calls out under it
    (every frame's yield, the bar's task), which a port could only match with
    a flag note. A way to declare a saved P's flags dead where liveness has
    them dead after its `PLP` (the sibling runs' frames, the sounds' P under
    the driver's call) would take the notes out of `menu/equipment_bridge.c`
    and `menu/load_screen.c` and let these two be ported;
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
  - the records' task stops at its yield (`$C2:F696`) every frame and the
    task scheduler (`$C2:821E`) is the original's, about 9% of what the
    menus interpret: the tasks as fibers of their own (`runtime/port/fiber.h`)
    would take it off the machine;
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
  - a bridge that falls back to the original (`menu_back` stopping at its
    return because the machine didn't come back as the port expected) is
    counted nowhere: the entry's calls still match, since the original runs
    the rest, so a check that fails on almost every call looks like a clean
    port. The per-frame input's check after the play time's tick held the
    tick's page to the controller's and fell back on nearly every call until
    a debug build showed it. A per-site count of those fallbacks in the
    shadow report (native.c's entry stats) would show it at once;
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
  - a record's load calls the unpacker (`$C2:F542`, the decompressor's port
    on the machine) as a call-out; the unpacker as C on the game's state
    would take the menus' loads off the machine;
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
- **Scene cells the menu objects use.** The C6 irises and motion routines
  (`menu/object_commands.c`) run on the menu's direct page and use the circle
  drawer's and the scaler's cells, which the map files under the scene; their
  drafts now give those cells scratch names (REIMPLEMENTATION.md, "The tooling pass
  after wave 1: the analysis"). Filing them where both subsystems see them (`core`, or
  entries on the menu's page) would name them again.
- **Stops after a call-out that changed the widths.** Ports that give up there
  (`battle_step_effect_slots`' `call`, the menus' `widths_are` checks) leave PC
  at the call's return address without declaring it; with a don't-care set on
  the entry, shadow mode and `ct_fuzz` report it if it ever happens, but the
  check can't see the point. None of the movies reaches one; declaring them
  (`m_stop_dont_care` at the return address) would make them checkable.
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

- **P3's leftover is not a leftover**: the renderer is fed by decoding PPU
  register writes, so the register file is still in the shipping path. Feeding
  it `vid_*` calls instead is what "no emulation backend" needs, and it is
  scheduled with the audio in PLAN.md, "Phase 4a". **P4** (widescreen) and
  **P5** (GBA feasibility) follow it in Phase 4b; P0-P3 are otherwise done but
  for the leftovers below.
- **The gates the event engine opens write their own registers.**
  `saves/events-end-of-time-gate` is the one movie `--vid-check` is not close on
  (3863 of 5599; PLATFORM.md, "P2 as built"): from frame 3864 its gate screen
  drives the Mode 7 matrix, the screen mode and the brightness itself, and none
  of that code is ported, so nothing tells the observer. It is a different
  screen from the Epoch's time dial (`menu/epoch.c`), which agrees on every frame.
  Porting the event's version is what closes it.
- **The attract demo's scenes write their own registers.** `attract-demo`
  agrees on 9379 of 22499 frames under `--vid-check` (PLATFORM.md, "P2 as
  built"): the scene program's modes 1 and 3 (`$7E:66DD`-`$7E:6CAB`,
  unported) write VRAM, the tile maps' bases, the mode and window 1 from
  frames 2789 and 8953, and on two of its world maps BG2's horizontal scroll
  (from 2266, on `$1F5`) and window 1's right edge (from 5492, on `$1F7`)
  change where no call says so; which code writes the last two isn't known yet. With the
  event's gate, these are what stand between P2 and a library that agrees from
  power-on.
- **P3 leftovers** (PLATFORM.md, "P3 as built"): the observer doesn't fill in
  the per-span memory pointers the decoder now does, which it needs before the
  renderer can be fed by service calls rather than registers. The native
  renderer is a wash against `render.c` rather than faster (the milestone's 81%
  figure predated `render.c`'s line cache), so the speed case for it is still
  unmade; what remains to win is the decoder, which a call-fed build wouldn't
  run at all.
- **Host backends beyond SDL**, none of them scoped as milestones yet. Game code
  reaches the host only through the platform services, so each is a backend
  behind an existing API rather than a change to `game/`:
  - **Rendering on the GPU** (Vulkan, Metal or GL). It would be a third consumer
    of `VidFrame` beside `render.c` and P3's renderer, with `--px-check` as its
    conformance harness. What it implements is SNES compositing, not arbitrary
    geometry: per-line state (`VidSpan`) makes a frame scanline-shaped rather
    than a few quads, and the colour math has to stay integer to keep
    pixel-identity as the test oracle. It wants feeding from the observer rather
    than the decoder, so it waits on the P2 and P3 leftovers above.
  - **Audio.** Output is already SDL2's audio device, which covers WASAPI,
    CoreAudio and ALSA/PipeWire, so another output layer buys little. The real
    item is PLAN.md's Phase 4a, no longer optional or later: a note-stream
    comparison (SOUND.md, "Validating the port") for when the lockstep
    reference is gone.
  - **The native mixer's sound memory.** It is a copy of the SPC700's 64 KiB
    with the original driver's layout, which is what keeps the echo's
    overwrites; a build that loads the samples as assets will want them out of
    it, with the echo's buffer still lying over the ones the original lets it
    reach.
  - **The native mixer on a small target.** `SoundHost` holds two ~80 KiB
    mixers (the live one and a state read's scratch) beside the APU's 64 KiB,
    and the output rate is fixed at 32 kHz. Left from the mixer's review: the
    desktop host loses nothing by it, and what a small target needs instead (a
    state read in place, a rate the host asks for and a resampler) is that
    target's to decide when one is scoped.
  - **The effects check's scaffold reads with the C sound driver.** The route
    check plays the driver that ships, but the scaffold reads come from the
    vblank check's shipping-mode runs, which play the original driver because
    they replay movies (the C driver's timing would drift from them). A
    `--scaffold-reads` in `ct_drive` would collect them over the routes too.
  - **The C sound driver's leftovers** (SOUND.md, "The port as built"). The
    paths neither the movies nor the fuzzer reach: the saturation at `$0A34`
    (unreachable by its arithmetic), the loop poll's retry at `$0B9B` and the
    ignored commands' echo at `$0BD5`, and the wrap of event `$F5`'s loop pop
    (`$1C2B`, `$1C54`). Three of the original's failures the port doesn't
    reproduce, and the fuzzer steers round: two `$FC`s queued behind each
    other run each other for ever; song `$3F`, the one long enough to run over
    the effect table, leaves an effect or a set switch playing its bytes as
    addresses anywhere in sound memory, which the port doesn't keep; and a set
    switch under a track that has run on into the effects.
  - **The house's "tinny" hi-hats** (SOUND.md, "What playing it found"). A
    report from playing `--sound-driver c` that no movie reproduces: over
    new-game the C driver plays the house's songs note for note as the
    original does, from the same samples. If it recurs, record the session
    (`ct_play --record` plays the original driver: note the route and the
    songs, or quick save there with the C driver) and compare sound memory
    with the original's at the scene; the
    host's fault that overwrote samples is fixed, and a song keeps a block the
    echo overwrote in the original too (battle-magus).
  - **The sound lockstep's audio comparison as a check**
    (`ct_dev --sound-lockstep-audio exact`, SOUND.md "What playing it found").
    It isn't exact yet: from the intro on (80.7 s of every power-on movie) the
    effect voices' levels part for a few hundred samples, and the long save
    movies part further. Find which writes land at another sample (the pairing
    of the C driver's writes with the original's is by register and value
    within a tick; a command's writes and the flags' several writes are the
    suspects), make it exact, then have a difference fail the run.
  - **Sound driver follow-ups after its review.** The qualitative review of
    `game/sound/` rated no file a translation (`driver.h` hand-written, the
    rest mostly), so it merged with these as follow-ups rather than blockers,
    the user's rule for "mostly" findings. Each is a place where the original's
    storage still shows through the model:
    - Per-track facts kept as the original's per-voice bitsets in
      `SoundVoiceFlags` (echo, noise, pitch modulation, sounding, held, slur,
      roll): they want to be bools on `SoundTrack`, with the voice sets the mix
      needs derived from the tracks.
    - Signed values in unsigned storage: `falling(step) { return step & 0x8000; }`,
      `(uint16_t)-step`, the 24-bit pitch-slide step in a `uint32_t`
      (`SLIDE_STEP_BITS`).
    - Packed bytes stored raw and decoded at every use: a wave's `depth` and
      its shape, an instrument's `tuning`, `pan_sweep`, and `octave` holding
      semitones rather than an octave.
    - `pan_sweep`'s bit tricks in `sequencer.c` (the event's setup of period,
      step and phase).
    - The look-ahead re-implements the loop, jump and branch control flow
      beside the events', reads operands by position, and changes state as it
      looks (it is the only thing that ends a slur or roll, which is why
      events `$E5` and `$E7` are no-ops): it wants to share the events'
      decoding and flow, and leave the ending of articulation to the events.
    - Four versions of "set a value or slide it there" (`slide_to`,
      `slide_toward`, the fades', the filter's), and the resumed song's fade
      step hard-coded (`RESUME_FADE_STEP = 0x0EF0`) rather than computed from
      its steps.
    - `start_effect` finds the effect's tracks by a byte scan that ends on an
      8-bit index wrapping.
    - `SILENT_TRACK`, the stopped effect's sentinel, points into the driver's
      program (`$1DC1`, an end-of-track byte) rather than being a state.
    - Unexplained adjustments to `ticks_left` and a loop's `counts_left`.
    - The instrument and percussion tables are kept by their sound-memory
      addresses, because the original's tables alias (the index is `2n & $FF`).
    - The status is stored as the four port bytes rather than as the three
      values the game reads.
    - Smaller: `OWN_SAMPLE_ENVELOPE = 0xE0FF` is a raw ADSR register pair;
      `(uint8_t)~pan`; `phase >> 7` with no unit; a literal `8` for the echo's
      taps (`SOUND_ECHO_TAPS`); `update_voices` and its loading-tick copy
      duplicate each other; `stop_music` followed by restoring the song ids it
      cleared (the music fade's end).
  - **Controllers.** `in_host_set` is already the one host entry point per
    frame, so this is an SDL GameController call into it plus bindings (see
    Player and tooling).
- **Assets as data files rather than ROM reads** (PLAN.md, Phase 4a,
  "Assets", which decides the shape: a catalogue, an import once into a pack
  in the user data directory, loaded by name through the driver, raw before
  decoded). Still open: the pack's format and its versioning; whether the
  battle scripts get a readable form before MD3; which codecs the importer
  needs beyond MATCHING.md's ("Build-time extraction").
- **Open questions** in PLATFORM.md: battles playing out differently once
  timing changes, widescreen's exposed assumptions (the 0-255 object window at
  `$C0:A88D`, map data beyond VRAM, single-byte battle window positions), and
  GBA's constraints.
- **P1 leftovers**: `$217F` mirror writes still come from unported video code
  (the battle's triangle and circle rasterizers `$CD:3810`-`$CD:3B05`, which
  write the window table through the work RAM port with 16-bit stores, so each
  edge's low byte also lands on APU port 3, as the quadrilaterals' `$D1:F9E7`
  did before it was C and left that byte as scaffolding, `hw_port_leave_spill`;
  history/reimplementation-waves.md, "Phase 3, wave 1"),
  and the APU-direct backend's audio comparison misses one measure on
  `new-game` (PLATFORM.md, "P1 as built").
- **The sound driver's waits on time and on the SPC700** (`core/sound_driver.c`):
  the boot's countdown at `$C7:011B` ($0800 passes, time for the SPC700 to start
  the driver) and every echo wait stay loops on the machine as idle entries.
  The Phase 4a port of the driver needs none of them -- there is no SPC700 to
  wait for once its program is C -- so these loops are deleted by that work
  rather than fixed by it.

## Timing without a beam

PLATFORM.md, "Timing without a beam", is the design: the census of every use of
the timing registers, what each is for, the native backend, and the rewrites
below; its "as built" says what is done. `python3 tools/timing_census.py`
re-runs the census.

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
  idle entry on the original's machine; a host playing the C driver answers
  at once. The native backend wants it as a wait the audio service owns (a
  call that comes back once the song plays).

- **The host backend in a build.** `runtime/platform/native/` has timing
  and input as a host runs them, checked only by `timing_test` on a made-up
  game: nothing links it with the game until `ct_native` (PLAN.md, "The native
  program", step 4). Still to write for it: `VID_AT_LINE_HANDLER` resolved
  from the recorded line by the native renderer (the picture's height,
  `tm_host_picture_lines`), and the other services' host backends.
- **The menus' tasks are coroutines of their own.** The scheduler is C
  (`menu/tasks.c`), but each task runs on its own stack and yields from deep
  in its calls (the description typed a letter a frame, the cursor's sprite),
  so a build with no machine runs a fiber per task besides the main thread's
  (the host backend resumes whichever fiber waited), or the tasks rewritten to
  resume, each a state machine called once a frame: the way for a target that
  can't afford nine 512 KiB stacks (PLAN.md, "What that makes
  scaffolding").
- **The clock's controller read inside the effect work** (history/reimplementation-chunks.md, "Phase 3,
  battle chunk 4"): the clock's modifier (`$C1:EE27`) runs the frame's input
  work again (`$C2:8002`) mid-work, and the port (`input_take_frame`) can't
  stop where the original waits for the console's controller read
  (`$C2:84F9`) or resets for the reset buttons: it goes on with the sets and
  the play time as they are. No movie reaches either; both want the input
  frame as a `tm_`/`in_` call that has an answer without a wait.
- **The event commands still on the machine** (history/reimplementation-phase3a.md, "Phase 3a: event
  onto the field's objects"): 21 bridges, rows of the language's table that
  call out to code still the original's: the sound driver (`sound.c`),
  `$D6`'s and `$D4`'s carries (`party.c`, whose calls to the menus'
  services are C now and keep the machine only for the Y they leave), the
  location change from
  `$C0:3B71`, `$4E`'s stack, and the unpacker (`$C3:0002`, still a
  machine-shaped port: `$82` and `$83`, `characters.c`, call it on the
  machine between their halves of C). A C unpacker taking a source and a
  destination would make those two operations.
- **What the party on the map left** (history/reimplementation-chunks.md, "Phase 3, field and events:
  the party on the map"):
  - an object is made pushable by bit 1 of the byte event `$84` stores, and
    `$84`'s row decodes that byte as solidity alone, its guard taking only 0
    or 1: a script making an object pushable (`$84` with 2 or 3) runs as the
    original's, and the library makes none. Decode the byte into the two;
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
  - a chest's gift goes to the party through the battle bank's inventory
    service (`$C1:8003`, A 1 for an item, 4 for gold) as a call-out. The
    service is C now (`battle/inventory.h`, history/reimplementation-chunks.md, "Phase 3, battle
    chunk 6"): the confirm button can call `battle_inventory_add` and
    `battle_give_gold` instead, timed as the service;
  - from chunk 2's review: name `field_exit`'s record size. The four collision nudges of the walk
    (`field/walk.c`, across, along and the two aslant) are near-identical and
    want one `nudge_around`; `look()` slows a climb as a side effect of
    looking, which wants to be visible at its callers. The climb's note
    reuses the layers' wrap cell (`field_layer_edge_wrap`'s low byte), a
    second meaning the map should name. The confirm button's `Search` keeps
    scaffolding-only fields (`passed`, `measured`, `last_measured`) beside
    the result. `battle_event_flags`' bits want one enum, shared by
    `event/party.c` (`PARTY_STAYS_PUT`) and `battle/sound.c`
    (`KEEPS_FIELD_SONG`);
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
- **What the larger sprite layouts left** (history/reimplementation-chunks.md, "Phase 3, field and
  events: the larger sprite layouts"):
  - the layouts' show rules differ between routines for the same object (the
    8-sprite placing shows sprites its rebuild hides); a native renderer that
    clips sprites itself would drop the rules, and the 24-sprite layout's
    "nothing to draw" from its last sum's carry, once the gate retires;
  - the sprite table's building runs as one stretch now, and its fuzzing
    stops at the access budget on random rows (before and after): a variant
    with a few objects in view would compare it;
  - `field_frame_staging_claims`' last four entries are cleared with the four
    areas' and claim nothing read so far.
- **What the frame builder left** (history/reimplementation-chunks.md, "Phase 3, field and events:
  the frame builder"):
  - a 12-sprite object holding a frame is never drawn and stays at the
    queue's head, so the queue spends the rest of its lines on it every
    frame; the port keeps that, and a native build without the scanline
    budget (PLAN.md's beam-coupled routines) has to decide what the
    queue does instead;
  - a frame is drawn into the staging areas in bank `$7F` and sent to VRAM by
    the queue the vblank empties; a native renderer could take the frame's
    tiles as they are drawn, and the areas, their claims and the queue would
    go with the gate;
  - the guard refuses graphics built in bank `$7F` whose tiles the frame
    names run past the bank (the copies would read low RAM through bank
    `$80`, the builder's own cells among it, which the port writes at other
    moments); no object the game makes does.
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
  - the switch's sound is still the original's routine (`$C0:1B90`, which
    pans it on the leader by reading the objects' screen X with the party's
    first two places as one index word); the port calls it and hands it A's
    high byte as the original has it (field/leaves.c);
- **What the screen effects left** (history/reimplementation-chunks.md, "Phase 3, field and events:
  the screen effects and the credits"):
  - the curtain's sweep is still bank `$C3`'s (`$C3:01E4`, through the jump
    at `$C3:000E`): each frame it moves every line's edge on by a few pixels
    picked by the scene engine's random mixer (`$C3:02DD`), which reads the
    beam's position (the H/V counter latches), so it waits on a timing
    service before it can be C; the attract demo's curtains (512 steps in
    `attract-demo`) are compared inside the field's frame, and fuzzing checks
    around the call and at it;
  - the ripple (event `$FF` `$9E`) and the field's close into scene `$85`
    (`$9B`) run for frames with the original's routines between their steps
    (BG1 drawn and its tile map readied, the frame's effects, the frame
    wait, the field put aside); no movie reaches either, and fuzzing
    compares them at their first call-out (BG1's drawing), past which they
    wait on the vblank: both are unchecked past it;
  - event `$FF` `$96` (the field's restart from its loader) is the
    original's from the command's own dispatch (`$C0:4009`), where the port
    stops; event `$FF` `$9F` calls the menus' party routine 8 as C through
    the same dispatch's frame (`event_screen_party_routine`);
  - the credits' letters' loading (`$C0:6E27`) and the layers' staging
    blanked (`$C0:75A0`, the block move from `$D2:0000` the map scrolling's
    follow-ups name) are call-outs of event `$29` and the layer work;
  - one fuzzed state in 40,000 of the screen effects' step differs only in
    OAM bytes holding what looks like the original's stack (seed 1, state
    10,647): find what writes OAM there.
- **The line effects' tables past their size on fuzzed states.** Fuzzing
  `field_build_line_effect_tables` and the window and wave builders over
  random states reports the accessor out of range: a table built for a random
  window or wave runs past its $57 bytes (`field_line_effect_tables`) into the
  next, from byte $57 on. It predates the Phase 3 chunks (the same run of
  chunk 10's build reports it) and matches the original, which writes there
  too; no movie builds such a table. Bound the rows' window and wave cells
  to what the game sets, or find what keeps a table within its size.
- **What the map scrolling in left** (history/reimplementation-chunks.md, "Phase 3, field and events:
  the map scrolling in"):
  - the rows BG1's and BG2's builders end with the second half of the map
    tile past the last, where the first would continue the line; it lies
    past the screen's right edge, and a native tile map that draws the view
    rather than the original's 64 columns (widescreen) would show it: build
    the first half there once the gate no longer compares the staging;
  - the staging buffers are the original's, sent by the deferred uploads a
    run at a time: a native renderer could take the rows and columns as
    tile map writes directly;
  - the map's whole-screen drawing leaves the edge runs' wrap cell 0, the
    count its loop ran down (`field_layer_edge_wrap`'s low byte), which the
    edges then read: a third use of that cell, with the climb's note;
  - the blank that clears the staging at `$C0:75A0` (a block move from
    `$D2:0000`) and the map's other loaders around the drawing (`$C0:7199`,
    1104 bytes left in the run) are the original's.
- **What the screen's setup and the objects' setup left**
  (history/reimplementation-chunks.md, "Phase 3, field and events: the screen
  set up, the map unpacked, the objects started"):
  - a switch pressed again after a menu or a battle shows the one-by-two
    switch's upper tile as the lower one's pressed tile (the original sets it
    so, `field/switches.c`): a native build decides what it shows once the
    gate no longer compares the map;
  - the screen's setup runs the original's wait for the vblank with the
    handlers off through `tm_wait_frame_then_enable` on the SNES backend
    (`$C0:0B38`, an `[[entry]]` hint so that liveness knows it as a routine);
    the host backend waits a frame and turns them on;
  - the dialog window's look (its frame and text area tile maps, the window
    design's tiles and colours, the choice pointer) is built into work RAM
    and sent as the original's transfers: a native renderer could draw the
    window from its height and position directly.
- **Bridges called from the engine are compared at the engine's boundary.**
  The engine calls a bridge directly, so shadow mode sees its call-outs as
  the engine's and abandons the bridge's own comparison ("started inside the
  engine's stretch"): the bridges left (the sound commands, the menus' party
  routines, the location change, `$4E`) are counted abandoned rather than
  matched, while the engine's entries match on every call. They are still checked (every call-out's registers, the
  engine's state at its end), and fuzzing covers them one by one; a way for
  shadow mode to compare a nested port's stretch would count them again.
- **Event's scaffolding, and what would let liveness drop it** (history/reimplementation-phase3a.md,
  "The event language"). The `ORIGINAL_EFFECT`s and the parallel table
  `event/leaves.c` reproduce what nothing in the library reads but shadow
  mode:
  - *V* is never read after a command (1.5 million commands over the
    library); the only read of it is a PLA in the sound driver's error path
    (`$C7:0A90`) that discards a pushed P. Liveness keeps it live because the
    field loop leaves through JMLs it can't follow. Following those JMLs (a
    hint per site, or the runtime's call log carrying JML edges) would let
    `DC_V` be declared at the engine's entries and every handler's, and the
    whole V column and its derivations go.
  - *Y* is read in one place, `STY $C0` at `$C2:8C36` (the menus' dispatcher,
    after the openings at map setup and the `$C0:595C` run of object 0),
    which stores its low byte at `$04C0`; nothing the library runs reads
    `$04C0` afterwards, and the menus' port shows the service those calls
    ask for (`$0B`, what the party wears) ignores it (`menu/party_bridge.c`),
    so `DC_Y` holds at `$C0:9DC3` and the parse column goes, but only
    once `$C0:597D` is ported too (it reaches the handlers' entries as the
    original's code; `$C0:595C` is the engine's now). The sound commands' calls to the driver keep Y
    and A's high byte for the same reason.
  - *The scratch cells* (`$D9-$E4`) and the command cells (`$C1-$D2`, the
    saved position among them) are read only by the command that wrote them --
    with one exception, below. They go when the gate does.
- **The menus' wave phase starts from event's scratch cell.** The wave handler
  at `$C2:77C7` (`menus-end-of-time`) reads and advances `$01E2` as its phase
  (`menu_wave_phase`, `menu/wave.c`: it means its object's own, and reaches
  this one through an index its loop has run on) without setting it first, so
  it starts from whatever event's calls (`$02`) or `$B5` last left there. A
  native build without event's scaffolding would start it elsewhere: give the
  phase a start of its own when the gate retires (a line or two of the wave, a
  frame).
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
- **Fuzzing half the event states.** The event handlers' guards turn away an
  odd running object, and `ct_fuzz --fill` can only bound a byte, so half of
  every tight run is skipped (more for the comparisons, whose operator must be
  in its table). A `--fill` form for even values (or a step) would double what
  each run compares.
- **`dontcare_check.py` takes a helper's parameters as a cross product.** A
  call-out's or stop's address passed down through helpers is evaluated over
  every value every caller passes, so a helper shared by two kinds of caller
  (the turning and the keep-facing moves) is checked at addresses no call
  makes, and a site read from a struct through a pointer isn't read at all.
  Event's movement bridges were reshaped around it (tables of rows, no zero
  sites); following a table row's index through the callers would let them
  say it plainly.
- **Frame waits still made on the machine**: the races `$C0:3B71` and
  `$C0:4181` (`field/frame.c`); the attract's `$7E:31B0`/`$7E:31B7`
  (`attract/main_loop.c`, which reads `$4212` itself rather than through the
  service); the idle entries on flags a handler clears
  (`field_wait_for_frame_loop`, `menu_wait_for_frame_loop`,
  `menu_wait_ready_loop`, `battle_wait_window_slide_loop`,
  `battle_wait_graphics_loaded_loop`, `scene_race_wait_for_tilemap_upload_loop`,
  `attract_wait_loop`, `warp_wait_for_frame_loop`); the time warp's wait for
  the vblank before it turns its picture on (`warp_screen_on_wait_for_vblank_loop`,
  `$FD:EADA`, reached through `tm_wait_frame_then_enable` as the field's
  `$C0:0B38` is). Ports that called the modes' wait routines call
  `tm_wait_frame` (`field_wait_frame`, `battle_wait_frame`). The battle's
  wait routines are ports whose loops give the processor up through
  `tm_wait_for_handlers` (`battle/wait.c`; PLATFORM.md, "Timing without a
  beam, as built"); the field's `$C0:EC60` (2.5 M instructions of the
  reference play, the largest wait left) and the sprite-object mode's
  `$C2:0346` are still the original's around their idle loops, and the pass
  that leaves an idle loop stays counted as a fallback: they take the
  battle's shape.
- **What the battle's waits left** (REIMPLEMENTATION.md, "Phase 4a, timing: the
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
- **The line handlers' hblank waits**: `field_irq_wait_hblank_loop`
  (`$C0:ECEB`), `battle_irq_wait_hblank_loop` (`$D1:FEB8`) and the scene
  boot's `$C3:0530`/`$C3:0538` land the forced blank at the end of line 211,
  which `vid_raster_display` at the handler's line already says. They read
  through the SNES-only `tm_snes_video_status` and have no host half; they
  go when the line handlers become functions the driver calls.
- **The battle's wait for a line `$CD:3B82`**: its callers call
  `tm_wait_line`, whose SNES backend waits where the port stands in the
  shipping mode and calls the original's routine only in shadow mode and the
  tools (`native_wait_call`). Its purpose isn't established: the frame work
  waits for line 64 before the screen script channels run while the timed
  effect slots do (`$CD:09AC`), and screen script command `$C1` (`$CD:1D93`,
  in no movie) waits for a line from the script. Neither is followed by a
  register write, so the wait looks like pacing; take it out when the frame
  work's own remainder is ported.
- **The scene program's noise effect**: `$C3:01E4` (called from `$7E:6844`,
  and from bank C6's `$C6:E27E` through `$C3:000E`, which no movie runs)
  still calls the original's stir `$C3:02DD`; it takes `scene_draw_random`
  (scene/random.c) when it is ported.
- **The timer resets.** `$C2:0043` past `$C2:00F2` and `$C2:940D` past
  `$C2:94B8` clear `$4207-$420A` in a run of CPU register writes (where the
  menu ports stop). Nothing enables the timers after them, so natively they
  are nothing; the boot (`$FD:C000`, `core/console.c`) and the time warp's
  screen (`$FD:EB81`, `warp/picture.c`) make the same clear through
  `tm_reset_timers`, which these can take too.

## Targets beyond the desktop SNES build

Wanted, none of them scoped as milestones. Each says what the design already
carries and what it doesn't; what they share is that the game side is still 73%
generated code (PLAN.md, Phase 3), so anything needing the *game* to know
something -- a screen coordinate, a menu's shape, where a resource lives --
waits on M5 rather than on a backend.

- **A WebAssembly build.** The seam is right: `game/` reaches the host only
  through `runtime/platform/`, and the one frontend (`frontend/`, SDL2 in
  `window.c`) is what Emscripten ports best. Four things are in the way, in
  order of how much they cost.
  - **Call-outs run on their own stack** -- but only while there is an emulated
    machine to suspend. Every non-idle registry entry runs on a fiber
    (`runtime/port/fiber.c`, `job_run` in `runtime/port/native.c`), which is
    Windows fibers or POSIX `ucontext`, and wasm has neither. The targets
    delete most of the problem rather than solve it: a finished `game/` makes
    no call-outs. What stays is the game's own: its main thread, which waits
    for the next frame from deep in its call stack (PLATFORM.md, "Timing
    without a beam"), and the menus' cooperative tasks (`menu/tasks.c`), so a
    finished build still switches stacks each frame, or has Emscripten's
    asyncify around them, unless the tasks are rewritten to resume. **So the
    third backend is not scheduled** (PLAN.md, "What this plan deprioritizes"),
    and when a browser build is wanted it is for those coroutines.
  - **What the browser build is made of.** The default path generates C from the
    user's ROM at build time (GENERATED.md), and nothing ROM-derived can be shipped
    (CLAUDE.md), so a public build cannot contain it. That leaves a build the
    user compiles, or one that is hand ports plus the interpreter over a ROM the
    page loads at run time -- which makes M5's coverage the gate on it, and asks
    the same question as "assets as data files" above: the readers assume a
    mapped ROM image.
  - **A driver of its own.** The player advances one frame per call and hands
    out what it persists as bytes under names (PLAYING.md, "How the player is
    built"), so what the browser needs is a third driver beside `run_window`
    and `run_headless`: a frame callback, an audio worklet fed through
    `audio_fill`, and saves kept in browser storage. The rules that keep those
    seams from re-closing are in CLAUDE.md, "Writing host-side code".
  - **Speed**, which is unmeasured: the interpreter plus `render.c` per line, in
    one thread, with no `-static` build to lean on.
- **A native AYN Thor multi-screen port.** Two parts, and only one of them is a
  backend. The Android/ARM64 side is ordinary: SDL2 builds there, no screen
  geometry is baked into `game/` (PLAN.md, Phase 3), and the handheld's pad
  arrives through `in_host_set` like any other (a binding in its driver, as
  `frontend/bindings.c` is the desktop's). The second
  screen is the real question, because `VidFrame` is one frame of one screen per
  frame. Two shapes, and they cost differently: a *companion* view (party state,
  the map, an item list) drawn by the frontend from game state, which can read
  it by name from the RAM map (`re/ram.toml`, `game/ram.h`); or a
  *second game screen* in the DS sense, which needs the video service to carry
  more than one frame and the game to decide what goes where. Either way it wants
  P3's leftover first (the observer doesn't fill in the per-span memory pointers),
  since a frontend-fed screen is the same call-fed rendering widescreen needs.
  Native resolution is far above 256x224, so it also meets P4's questions about
  what lives outside the original screen.
- **DS and PlayStation content in the original.** The Dimensional Vortex, the
  Arena, the Lost Sanctum, the bestiary: this is content authoring, not a port,
  and it is the largest item in this file by a wide margin. What it needs, in
  dependency order: the scripts and text are typed with round trips now
  (MATCHING.md, "The string encodings", "The event scripts" and "The battle
  script formats"), so what a dungeon needs first is the **asset pipeline**
  ("Assets as data files" above), because
  new maps, sprites and music have no ROM address to read from, which is exactly
  what Phase 3's named resource accessors are the seam for; and the field, event
  and scene subsystems ported far enough to extend (`$C0` and `$C6` are both
  68% generated). Three things are worth writing down now so they
  don't surprise later: the DS and PS1 releases are a different engine and a
  different asset format, so this is re-implementation from a reference, not an
  import; their assets are as ROM-derived as ours and stay out of the repository
  wherever they live, extracted from the user's own copies; and the animated
  cutscenes have no path at all -- there is no video decode anywhere in the
  runtime, and adding one is its own decision.
- **A SNES ROM compiled from the hand ports.** Not byte-exact: a playable ROM
  whose routines are the C in `game/`, compiled for the 65816, with the
  original's assembly for whatever isn't ported. In dependency order:
  - **Relocatable source first.** The matching build rebuilds the ROM
    byte-identically from assembly, but every pointer is still a number, so
    nothing can move or grow. MD3 (every pointer symbolic, with the shift tests)
    is what makes a routine replaceable at all; it is the prerequisite for any
    ROM that differs from the original, compiled or hand-edited.
  - **Then a hybrid, one routine at a time**: link a compiled routine in place of
    the original's, keep the original's assembly for the rest, and check that the
    ROM plays. A call-out to unported code is a real `JSL` there, so the
    incremental shape the project already has carries over.
  - **Ports written for it.** The ports run against an emulated machine: they take
    a `Snes *`, read through `m_read8`, and the transcription-style ones move
    emulated registers about (`python3 tools/portstyle.py`: 62 of 86 files, 76%
    of the lines, which PLAN.md's Phase 3a rewrites before anything else). A
    65816 build needs each routine's logic apart from
    the original's calling convention, which is also what the readability bar
    asks for; CLAUDE.md's porting rules say so, and `tests/test_port_subset.py`
    keeps `game/` inside what a 65816 compiler handles (no floating point, no
    64-bit integers, no host C library -- none of which `game/` uses today). The
    platform services already fit: their SNES backend turns each `vid_*` call
    into the original register writes, which is what a SNES build compiles them to.
  - **Speed is the open question.** The CPU runs at about 3.6 MHz, the original
    is hand-tuned assembly, and compiled C is several times larger and slower;
    NMI and HDMA work that misses vblank corrupts the picture rather than slowing
    it. Which routines can be C at all is to be measured, not assumed, and the
    hot ones may stay assembly for good. Size too: about 240 KB of code in a
    nearly full 4 MiB ROM, so an extended layout (ExHiROM) is likely.
  - **The toolchain** is its own evaluation: 65816 C compilers exist (PVSnesLib's,
    Calypsi), and they differ in output quality, control over the calling
    convention, and licence.
- **Native touch controls**, rather than a virtual d-pad over the screen. A
  virtual pad is free today and needs nothing new: the frontend maps it to
  `PadBits` and calls `in_host_set`. Real touch is not a host problem, it is a
  game-side one. The input service is one 16-bit word per frame because that is
  what the machine reads; a pointer service beside it (`in_host_point`, position
  and pressed) is additive and cheap. What it buys nothing without is a game that
  knows where its UI *is*: menus and dialogs are drawn as tilemap writes from
  `$C2`, 90% of it still generated, so there is no element with a screen-space
  box to hit-test against. So the order is: the pointer service, then the menu
  and dialog subsystem ported to the bar where a selectable item carries its own
  rectangle, then a policy per mode (a tap on the field: a target to walk to, or
  a synthesized direction -- the object activation window at `$C0:A88D` makes
  that a real question, the same one P4 runs into). Screen coordinates are
  already signed by the porting rules, which is what keeps this from needing a
  second pass after widescreen.

## Movies and coverage

- **What the library doesn't reach, measured.** Over the 26 movies' coverage
  files against the analysis's code, **22% of the ROM's code (58.7 KiB) has
  never been executed**: battle `$C1` 16.0K (26% of the bank), menus `$C2`
  13.5K (23%), field and events `$C0` 9.9K (17%), the scene program in `$7E`
  7.9K (32%), scene data `$C6` 3.4K (**97%**), and less elsewhere. The largest
  never-run runs are `$C0:11C9` (1299 bytes), `$C6:E241` (1293), `$C2:50AC`
  (912) and `$C1:A737` (884). Recompute it by loading `traces/coverage-*.txt`
  and `traces/saves/cov-*.txt` with `tools/disasm/coverage.py` and comparing
  with the analysis's code addresses.
  - **Why it matters more as Phase 3 goes on**: generated code is correct on
    every path by construction, so unexecuted code is harmless while it stays
    generated, but a hand port there is only as good as its fuzzing. **86 of
    597 registry entries (14%) are never entered by any movie** -- `$C2` 28,
    `$C6` 22, `$D1` 9, `$C3` 8, `$FD` 7, `$C7` 6 -- and those rest on fuzzing
    alone. Each wave since the pilot has found bugs there.
- **Endings: three of about thirteen are recorded** (Beyond Time, the telepod
  ending, the bucket ending). Reunion needs a route to Lavos from "The Time
  Egg" (no Epoch reachable from that save); the rest are unrecorded and cheap
  now (MOVIES.md), since an ending is chosen by where and when Lavos is beaten
  plus story flags, and `oracle/saves/` has 51 saves to start from.
- **Content no movie plays at all**: most sidequests, and much of the endgame
  beyond the Black Omen. That is the likeliest home of the dark code in `$C0`
  and `$C2`, since sidequests are event scripts and menus. Driving them is
  viable headlessly the way the existing routes are (`ct_drive` plus
  `tools/movies`, which already carry `bucket_lavos` through 171005 frames to
  the credits): walking, talking and fighting are what the routes' pathfinder,
  `talk()` and battle bot already do. What needs bespoke code per route is
  puzzle mechanics a search won't infer and minigames needing timed input (the
  jet bike race is the precedent that it works). The costs are machine time
  (100k-170k frame runs), the addresses each route needs, and re-running a
  route when the timing model changes, since routes survive drift but the
  movies they record don't.
- **The four boss fights** (movies-wanted.md, "Boss fights") are unattempted beyond finding
  where each save starts. `ctsave02` slot 3 lands already inside Arris Dome at `$0D6`'s own
  room (Guardian's), not on the world map, but walking to the sprites visible there and
  talking to each of the dozen or so nearby objects (an `objscript` sweep, not a person's
  guess) found no battle trigger; the Guardian may need a terminal or switch elsewhere in the
  room, or a specific approach the sweep didn't try. `ctsave03` slot 2 (R Series, the
  factory), `ctsave07` slot 3 (Nizbel II, Tyrano Lair) and `ctsave14` slot 2 (MotherBrain,
  Geno Dome) all start on the world map at the dungeon's door instead, needing a walk in and
  down to the boss room neither attempted nor mapped this pass.
- **zophar `chrono-7`** (everything maxed at the fair) is unused: its Lucca
  exhibit gate (`$7F:0055` bit 7) can be opened, but no movie needs it yet.
- **Coverage loop**: re-run `--misses` over the whole library after the latest
  movies and feed anything new back into the build.
- **Battle setup is never compared.** Every call of `battle_setup_state`
  (`$CC:E2E4`) and `battle_setup_graphics` (`$CC:ED87`) in the library is
  interrupted by NMI, so shadow mode counts them and compares none; the gate
  passes on them without looking. What checks them today is `--native on
  --generated off` tracing identically to a known-good build (DISASSEMBLY.md,
  "The RAM map"). A fuzz state at their entry, or comparing interrupted calls
  when the interrupt touches none of their state, would close it. The same
  holds for the battle's start from the field (`battle_start_from_field`,
  `$C1:0000`): its call to the battle screen's setup waits for frames, so all
  its calls are interrupted, and fuzzing compares it only up to that call; what
  it does after (clearing the battle's memory, taking in the story's rules)
  is checked by the shipping-mode traces and routes alone. A member that comes
  back once is fuzzed up to its showing, which `ct_fuzz` can't run as a sibling
  (no runtime), so the registers at that call differ there.
- **Snes9x agreement tables** in RUNTIME.md were re-measured after the timing
  fix for `new-game`, `menu` and `field` only; `fair` and `fair-companion` got
  worse and are unexplained.

## Player and tooling

- **The fallback measure's edges** (REIMPLEMENTATION.md, "The fallback measure"):
  - it follows extents by the stack, so a task switch to a stack within
    `STACK_SPAN` of the one it left (`dev/fallbacks.c`) keeps the extent it
    was in, and a routine reached by a jump rather than a call is filed under
    its caller;
  - the shadow runs play the original sound driver, so the CPU half's waits
    for its answers count there (0.9 KiB) and are gone in the shipping mode;
  - the routes' counts include the attempts a route throws away and the
    frames a load replays, and a run `route_check` resumes loses the counts of
    the process it killed;
  - the shipping mode's count can't be ratcheted: it moves with every port's
    timing (99 of 1068 keys between the default and the native-shape build).
    A shipping-mode run that played the movies at the original's timing
    (`--port-timing oracle`, where it fits) would be the closer measure of
    what a player runs.
- **The movie suite's wall time is the longest movie's.** The gate
  (`tools/suite.py`) takes about 10 minutes at `-j 2` (PORTING.md, "What
  shadow mode costs"). The three endings are 54% of its CPU time and
  `endings-bucket-lavos` alone 24%, so from about `-j 4` up the wall time is
  that one movie however many workers are idle:
  - **Split the long movies.** A `--native off` pass (3-4 times faster than
    shadow) could save states at a few cut points, and the segments run in
    shadow in parallel from them, with their coverage and call files merged.
    The routines in flight at a cut would go unchecked there.
  - **Skip the shared prefix.** Every movie replays the attract sequence and
    title from power-on before reaching what it covers, about 1638 frames each
    across 26 movies, now about 5% of the suite. A state snapshot taken once at
    the point a movie's own input starts would cut that, at the cost of no
    longer validating the boot path 26 times over.
  - **Sound RAM in the shadow copies.** A refresh still copies the 90 KB
    outside work RAM and VRAM, mostly the APU's 64 KB, which with the
    comparison is about 15% of an ending's shadow run. Stamping sound RAM
    pages as work RAM's are (the SPC700's writes and the DSP's echo writes)
    would cut it.
  - **A fast gate**: `--movies` and `--only` narrow a run by hand, but deriving
    the subset that can reach the subsystem a change touched would make the
    iterating case automatic, with the full set still required to land. What
    the map chunk asked for on top of that is a re-run against the logs a green
    run already wrote: it paid the 45 minutes three times, twice for changes
    that were names and comments, because there is no way to say "nothing but
    constants moved since the run that passed".
- **The burndown ranks by bytes, and can't say a run is blocked.** `--uncovered`
  put `$C2:27DE` among the largest runs in the bank, but its 1385 bytes are
  about fifteen loaders whose only real content is *which resource each stream
  is*, so porting them to the bar needs the resources typed first (history/reimplementation-waves.md,
  "Phase 3, wave 2: the menus' map"). A chunk picked off that list can be
  unportable for a reason that has nothing to do with porting, and the map
  chunk lost an hour discovering it. A column saying how much of a run is calls
  into untyped resource tables would say it in the listing.
- **`ram_map.py coverage` stops measuring a file once it is converted**, and
  will drift from meaning what it says. It counts named addresses among
  *address uses*, so a file that reaches state through `game/vars.h` has no
  address uses at all and leaves the denominator rather than scoring 100%. As
  Phase 3a proceeds, "91% named" quietly becomes "91% of what is left
  unconverted is named". All of `game/scene/` is converted now, so the scene's
  column already means only what is left elsewhere; a second column counting
  converted files, or a denominator that includes accessor uses, would keep it
  honest before the big subsystems land.
- **`portstyle.py`'s function count** misses one-line definitions (its
  matcher wants the parameter list's closing parenthesis at the end of a line)
  and counts guards and entry wrappers with everything else, so "45% on the
  machine" for scene reads as a backlog when it is entries, guards and bridges
  (REIMPLEMENTATION.md lists them by name). Matching definitions properly, and splitting
  the column into registered entries, guards and the rest, would make the rest
  the number to drive to zero.
- **`tools/fuzz_routines.toml` has the scene's, the events' and the field's
  entries.** Each subsystem's Phase 3a rewrite adds its own from its PORTING.md
  rows as it goes; a check that every registered entry has one would say when
  the file is complete.
- **Scene buffers reached by address** (`grep -rn wram_ game/scene`): 34 uses
  in 8 files. The sequence scripts' bytes and the race's line-effect descriptor
  table are fixed parts of the program image at `$7E:3000` and could be named
  buffers of it; the scaler's rows, the bank `$FE` copies' destination, the
  title's plane tiles, the sprites' sources and lists and a store command's target are
  wherever the caller points, and want the caller ported to say what they are;
  the circle's table is the pilot's. The race's setup's (its palette as
  unpacked, the unpack buffer, the plane's rows, the racers' pictures and the
  mirroring's two cells) are fixed and could be named. The title loader's
  tile list at `$7F:0200`,
  which the original reads through the work RAM port, overlaps the race's scaler
  output (and the event variables), so the map can't name it in the scene's
  subsystem (`ram_map.py coverage --open`).
- **The title's loader is in fragments.** The title's graphics loader
  (`$7E:5978`-`$7E:5A00`) is registered a block at a time
  (`scene_title_load_*`), each named for what it puts in VRAM, because the
  loader around them calls the program's unpacker and its own routines
  (`$7E:3691`, `$7E:38AF`, `$7E:89ED`, `$7E:5E3C`). The race's setup was
  ported whole as a list of graphics, each unpacked and sent (history/reimplementation-chunks.md,
  "Phase 3, the scene program: the rest of the jet bike race"); the title's
  would retire five entries the same way.
- **The race's setup calls the original three times.** The program's unpacker
  by resource (`$7E:3691`, before the decompressor `$C3:0557`), the camera
  script's first step (`$7E:3749`, before the sequencer `$C3:0AFF`) and, for
  the race without its panel, the curtain's step (`$C3:000E`, the field's
  `$C3:01E4`) are call-outs; with the unpacker as C, the setup's machine
  (the frames and the data bank and direct page it keeps on the stack around
  them, `scene/bridge.c`) comes down. The setup's mirroring of the racers'
  pictures runs long loops of its own, which is why it carries timing
  exceptions; the arrangers (`$7E:3EB7`, `$7E:3ED3`, the latter the ending's
  too) timed as their own routines would fit it better.
- **The perspective's other scenes' steps are refused.** The plane's
  perspective (`scene/perspective.c`) builds the race's lines and the endings'
  (the routine at the program's call, `$7E:45B0` or `$7E:9B46`); modes 1 and
  3-8, which no movie reaches, write other starts (`$7E:8042`, with a step of
  two lines either way and an end at line 112) that the guard hands to the
  original. Port them with those scenes.

- **Scene work that is still the original's, beside its ports.** The title
  logo's sweep before it lands (`$7E:5CBA`, where `scene_title_move_logo`
  stops) and the ripple's new first offset each frame (`$7E:600E`, where
  `scene_title_ripple_columns` stops) are the program's code; porting them
  would end two stops mid-routine.
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
  - `script.c`: the register encoding (`(operand & 0x1E) / 2`) and the operand
    offsets the adapters spell out want the table to decode typed operands.
  - `math.c`: `FoundDivisor.tried` is the search's leftover, not what it
    found; and `divide_8_8` computes where a value lies along a range, so
    `fraction_along`.
  - `race_matrix.c`'s generator keeps running accumulators where each row's
    offset could be computed from its index.
  - The bank `$FE` resources (above) wait for the platform.
  - The title's logo sweep and ripple (above) wait for their Phase 3 ports.
  - `game/attract/` is the same scene program (its main loop and idle waits)
    and belongs in `game/scene/`; the two headers point at each other until
    attract is next reworked.
- **An endings concept.** The endings have no routines in `game/scene/` of
  their own -- they are the program's scripts, using the iris, the scaler and
  the arithmetic -- so there is nothing to hold one yet; when their script
  routines are ported, they want a file of their own.
- **`scene/scene_data.h` reads bank `$FE` by address** (`scene_data_byte`,
  which the resource copies use): the asset seam wants a named accessor for
  each resource the table at `$FE:0000` points to.
- **`scene_find_divisor` on a bank that isn't low RAM.** Its limit is read
  through the data bank, and its guard now leaves the rare call whose data bank
  doesn't reach low RAM (one in some runs of `events-jetbike-race`) to the
  original, since the cell it reads there isn't the scene's. Which caller makes
  it, and what that cell is, isn't known.
- **The RAM map can't say two variables alias.** `ram_map.py check` rejects
  overlap inside a subsystem, but the ROM overlaps: the map planes' last column
  word is the first plane's row word (`$7E:00E7`), which had to be modelled as
  two arrays of different lengths with the alias explained in prose. An
  `aliases` field would let an entry say what is true.
- CI: the macOS job costs 10x minutes on a private repo.

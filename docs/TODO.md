# Open follow-ups

Loose ends the milestone docs record in passing, collected so they don't get
lost. Each line says where the detail lives. Milestones themselves are in
[APPROACH.md](APPROACH.md), [RECOMP.md](RECOMP.md), [MATCHING.md](MATCHING.md)
and [PLATFORM.md](PLATFORM.md); which of these to work on next, and why the
rest waits, is [PLAN.md](PLAN.md).

## Correctness and accuracy

- **Hand ports' time is an estimate**, so `--native on --recomp on` drifts from
  the original (battles play out differently). That difference is accepted
  (RECOMP.md, "Can a hand port be exact?"); the timing gate bounds the errors
  (RECOMP.md, "The timing gate"). Left open there: 25 entries whose
  time follows a path inside one routine, or a loop of the port's own, are on
  `tools/timing_exceptions.txt` (the decompressor, the menu's text, glyphs and
  wave table, `field_rasterize_quad`, the field's sprite table, line-effect
  builders and map unpacker, whose rewrites no longer make the index steps
  the original's time follows, the field's object update, whose steps now
  run in its own call, the scene's circle, the battle's wave
  windows and HP gradient, the free-slot walk that starts a menu object, and
  the own loops of the effect slots, the object
  drawing and its underlays); a dispatcher's handlers are attributed to
  instead (RECOMP.md, "Dispatchers"), and the menu text's control codes would
  be too once their handlers are entries. Adding a movie or a port means
  recalibrating, since the drift limit holds only for the measured value.
  `--recomp only` stays exact.
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
- **`ct_fuzz` doesn't use generated blocks**, only hand ports (RECOMP.md).
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
- **The last of `$CF`**: `$CF:FAE2` (259 bytes) and `$CF:FD9E` (231) are the
  bank's unported runs (`python3 tools/burndown.py --uncovered CF`).

## Phase 3a work in flight

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

- **What core's controller rewrite left** (RECOMP.md, "Phase 3a: the
  controller off the machine"):
  - the rest of `game/core/` reads as translation though none of it is on the
    baseline (0.07 a line hides it): `palette.c` (every function on the
    machine, slots as byte offsets, the multiplier used to compute, the
    scratch pointer re-read per colour), `sound_driver.c` (the CPU half, on
    the machine throughout, names from `$2F00`), `tile_animation.c` (offset
    arithmetic into the slot records, the transfer's write order spelled out,
    call sites computed), `display.c` (the power-on setup as register bytes
    through the decoders, in the original's order) and `memory.c` (the block
    move stub as `m_mvn`);
  - the button names other subsystems still define privately: `event/input.c`
    (the game's order, `core/input.h`'s `BUTTON_*`), `battle/nmi.c` and
    `battle/frame.c` (the controller's, `platform/input.h`'s `IN_BUTTON_*`).

- **What menu's rewrite left** (RECOMP.md, "Phase 3a: the menus off the
  machine"):
  - `menu/epoch.c` is still on the transcription backlog (0.16 operations a
    line, 1,827 lines): the Epoch's two Mode 7 screens and the End of Time's
    gate, each a machine-level port of its own. It is the next piece, and the
    object mode's module (`menu/object.h`) is what its object handlers should
    be rewritten onto;
  - what the qualitative review rated "mostly", to finish:
    - `menu/script_bridge.h`: entry names that disagree with their operations
      (`reset_state` for `forget_everything`, `sound_18`/`sound_19`, and
      others), and the bridge switching on opcode literals (`0x3B`/`0x3C`/
      `0x4B`, `0x33`) rather than on the rows;
    - `menu/sprites.c`: the sprite-script interpreter is a switch with
      positional operands and a second table of lengths, where a table of rows
      would do (as `object_script.c`'s);
    - `menu/vram.h`: the object queue is still also set field by field by
      offset (`menu_set_object_upload_*`, which `object_script.c`'s tile
      groups use); `MenuObjectUpload` should be the only way in, with its
      `port` an enum rather than a byte;
    - `menu/object_script.c`: the look bits `0xF1`/`0x4F` belong in
      `object.h`, once; the story bits in `top_layer_stays` and the sounds
      `0x18`/`0x19` need names; `load_palette_row` keeps its parameters in
      `command_state`/`move_target`; `word`/`second_word` should be the
      parameters' names;
    - `menu/save_screen.c`: the portrait's character goes round through
      `ORIGINAL_EFFECT` and back; its inputs should be parameters; and the
      return site (`0xE7AC + 2`, `pc == 0xE7AF`) should be named;
    - `menu/text.c`: control codes decode their operands by offset;
    - `menu/map.c`: the logic's results are shaped by the entry and the
      scaffolding (`Strip`, `MapScroll` and the strip queues' `at`
      out-parameter exist for X and the leftover cells); the wrapper can
      derive X from the final state, leaving one scaffolding record;
      `redraw_plane` takes `Snes *` only for its timing tag and the platform;
    - `menu/line_effects.c`: the irises' bars and the capsule's gap are filled
      by an overlapping forward copy (`repeat_forward`), where a fill of
      lines says what they are; the iris tables at `$8DF6`/`$9176`/`$9416` are
      literals, not in `re/ram.toml`; the channel-write site is computed from
      PB:PC where a literal per entry belongs; the squashed iris steps byte
      addresses rather than lines;
    - `menu/object_commands.c`: still a file of "bank C6's routines" (the
      Epoch area check belongs beside the Epoch's screen), the scene loaders
      are named for resource numbers, the mode mask `0x3C` is unnamed, and a
      16-bit scaffolding store into the area's top cell has no comment;
    - `menu/epoch.c`: the gate matrix sets line slots 2 and 3 by offset from
      `GATE_MATRIX_TABLES`, where `line_effects.h`'s operations belong;
  - a divergence the rewrite accepts, on inputs no game state makes: a
    string's control code past `$0C`, or one included deeper than four, draws
    nothing, where the original jumps or recurses (`menu_draw_text`'s fuzz
    mismatches in PORTING.md);
  - the load screen's member rows keep their own place, where the original
    reads it back from its cells after the window list and the portrait (both
    still the original's) have run: a list run over random memory that
    changes those cells is where the two part (8 of about 3,400 fuzzed
    slots); the lists' port closes it;
  - the map's strips are built in the upload buffer where the game keeps it:
    `map.c`'s guards hand a buffer outside work RAM above low RAM, a plane
    whose top-left tile is off the map, or a queue without room for a
    scroll's strips to the original, none of which the library makes;
  - the load screen's slot is drawn as C from `menu_show_save_slot`, so shadow
    mode compares `menu_draw_save_slot` only where the original's own code
    calls it (1,139 of 15,708 calls; the rest are inside its caller's
    comparison);
  - `menu_saves_finished_game` (`menu/menu_data.h`) reads the cartridge's
    save RAM at `$30:7FE2` through the ROM accessor: a save-RAM service should
    take it;
  - the glyph drawer takes the font's addresses from the text engine
    (`$C2:5DC4`, still the original's), so a font loaded from elsewhere needs
    that engine ported first.

- **What event's and field's combined pass left** (RECOMP.md, "Phase 3a: event
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
  - `$B0`'s bridge (`event_cmd_behave`): thick, its roles used as indices
    into tables of ROM addresses (review-port item 42), the machine's
    registers edited directly, and its comment's "wandering townsperson",
    which it isn't (only party members reach the state routines); it becomes
    an operation when those routines are ports;
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
  nothing else. A fuzz setup that bounds its draws would close that.

- **What only the original's memory has, past the object tables.** The
  stray accesses are explicit and marked now (RECOMP.md, "Phase 3a: event onto
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
  which has nobody in an empty place, and these would do the same (RECOMP.md,
  "The absent party member and the object off the map").
- **What the accessor checks' fuzzing left** (RECOMP.md, "The accessor bounds
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
  (RECOMP.md, "The accessor bounds checks"). Find what differs in the suite's
  runs before reading the gap as the checks' cost.
## Recompiler

- **Stops and entries don't get the strict solve yet.** The don't-care check
  holds call-outs to liveness that trusts no target set the machine picks at
  run time (RECOMP.md, "Call-outs into targets the machine picks"), but not a
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
- **The draft tool's gaps** (RECOMP.md, "What drafts don't do yet"): D and DB
  for code only interrupts reach, and the field's main loop after a battle,
  which the frame analysis gets wrong (a guard catches it: 432877 calls of
  `field_scroll_layers`); operand names for the battle script handlers;
  turning I/O register writes and the multiplier into platform calls and C
  arithmetic; stub signatures that still vary by what a call reads back.
- **Drafts and ports don't use the new declarations yet** (RECOMP.md, "Between
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
  `ret_live_mask` (in `tools/recomp/show.py`, not in `liveness.py`) asks
  `live_after_return` at the entry's own contexts and falls back to
  `after_return` only where that is unknown. Both are defensible and neither
  says so, which cost the map chunk time and left it reproducing registers it
  could have declared dead. Moving `ret_live_mask` into `liveness.py`, emitting
  it in the JSON beside the other, and naming in each docstring which question
  it answers would settle it once.
- **The scene sequence interpreter's hand-back** (`scene/script.c`): a
  command the port doesn't take is given back as a call-out to the loop from
  the entry's `JSR` site, which shadow mode compares at the loop's first pass,
  right only when no command ran before it (no movie has such a command).
  Stopping at the loop's head `$C3:0B03` instead is what shadow mode can now
  compare at the right pass (RECOMP.md, "Between the waves: the validator"),
  and since the rework the port decides whether it takes a command before it
  writes anything, and the flags the machine should have there are in P after
  every command (`note_flags`); so the stop is what is left to try, with
  a fuzz comparison against the call-out.
- **Liveness across a saved stack pointer.** The overflow flag is still live
  after `$FD:C2C1` and the rest of the field loop's callees (the carry and X
  no longer are): the menus' entry `$C2:800E` saves P and then copies S to
  `$0920` (`TSX`) to run on a stack of its own, which the pairing of pushes
  can't follow, so its `PHP` reads every flag for every caller. The same holds
  across the menus' task switch (`$C2:821E`), and for the scene program's task
  lists (`$7E:3994`, `$7E:39C8`), whose tasks leave through `JMP $39A7` into
  the loop's own code and carry on with the next task rather than return.
  Following S through the cell it is saved in, and a callee that finishes its
  caller's iteration, would free them (RECOMP.md, "The tooling pass after
  wave 1: the analysis").
- **Scene cells the menu objects use.** The C6 irises and motion routines
  (`menu/object_commands.c`) run on the menu's direct page and use the circle
  drawer's and the scaler's cells, which the map files under the scene; their
  drafts now give those cells scratch names (RECOMP.md, "The tooling pass
  after wave 1: the analysis"). Filing them where both subsystems see them (`core`, or
  entries on the menu's page) would name them again.
- **Stops after a call-out that changed the widths.** Ports that give up there
  (`battle_step_effect_slots`' `call`, the menus' `widths_are` checks) leave PC
  at the call's return address without declaring it; with a don't-care set on
  the entry, shadow mode and `ct_fuzz` report it if it ever happens, but the
  check can't see the point. None of the movies reaches one; declaring them
  (`m_stop_dont_care` at the return address) would make them checkable.
- **Gaps in the vblank check** (RECOMP.md, "Where video writes land"), which
  the suite runs with seven findings accepted (RECOMP.md, "The timing gate"): a
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

- **The ports still on the older video forms** (PLATFORM.md, "The services'
  gaps the reviews found, closed"). The battle and three of the menus' files
  still hand the SNES backend their write order (`VID_STEP_*` in a
  `VidSnesXfer`) at the call. Moving a call to `vid_send_*`,
  `vid_set_line_slot` or `vid_snes_copy` means adding its site's sequence to
  `runtime/platform/snes/encodings.c` and, once a file has none left,
  deleting it from the lint's `WRITE_ORDER`. When none is left, the older
  forms (`vid_upload_*`, `vid_fill_vram`, `vid_begin_vram_uploads`,
  `vid_set_line_effect`, `vid_snes_transfer`) and `VidSnesXfer` leave the
  service. What is left, for the battle rewrite and the menus' next pass:
  - `battle/display.c`: the palette (`$CD:09CE`), the seven per-line slots
    (`$CD:09F1`), and the graphics load's and the tile rows' uploads
    (`$CC:E000`, `$D1:E8D9`);
  - `battle/frame.c`: the vblank's uploads at `$CF:E300` (the queued pair, the
    sprite table, the escape at `$CF:E33B`, the window frames' tilemap, the
    palette and the upload list), the flushed transfer (`$CD:3E95`), the
    sliding window's fill (`$CD:066F`) and the window slot (`$CD:0AF0`);
  - `battle/nmi.c`: `vid_begin_vram_uploads` at `$D1:E7D7`, which programs no
    channel and becomes `vid_start_vram_uploads` as it is;
  - `menu/epoch.c`: the Epoch's screens' and the gate's line slots
    (`vid_set_line_effect`) and their Mode 7 transfers (`vid_upload_vram`,
    `vid_upload_vram_half`);
  - `menu/map.c`: the two strip uploads (`$C2:0AF6`, `$C2:0B22`);
    `menu/line_effects.c`: the irises' slot.
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
  screen from the menus' gate (`menu/epoch.c`), which agrees on every frame.
  Porting the event's version is what closes it, and it is the last movie
  between P2 and a library that agrees from power-on.
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
    items are PLAN.md's Phase 4a, and they are no longer optional or later.
    **Make the C sound driver the one that plays** (`game/sound/`, identical to
    the original tick for tick; `ct_play --sound-driver c` plays it on the
    host, SOUND.md "The port as built"): play it enough to make it the default,
    then drop the protocol for calls, which deletes the CPU half's uploads and
    waits (`game/core/sound_driver.c`) and the host's answering of them
    (`frontend/sound_host.c`). Until then the player can't save or load a state
    with it, since the driver's state isn't in a saved state (saving the
    `SoundDriver` beside the machine would do). Then **a native mixer behind
    the voices** (`platform/audio.h`, "The voices") that decodes the same BRR
    samples and runs the same envelopes and echo, replacing
    `runtime/apu/dsp.c` in the shipped product, and a note-stream comparison
    (SOUND.md, "Validating the port") for when the lockstep reference is gone.
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
- **Assets as data files rather than ROM reads.** The matching build already
  extracts every typed data region from the user's ROM (MATCHING.md,
  "Build-time extraction"): raw blobs, and decoded forms where a type has one -
  decompressed streams, BRR as is, graphics as planar tiles. Three things are
  missing before that is an asset pipeline. A layout meant to be *read* rather
  than reassembled, and stable enough to depend on. The script as text: the
  strings and the event scripts that name them decode to a readable form now
  (MATCHING.md, "The string encodings" and "The event scripts"), the battle
  scripts don't. And a decision about whether the runtime
  should load from such a directory at all, since APPROACH.md's conventions
  have assets loading from the ROM image at run time and every reader today
  assumes that. A GBA build is what forces the question, because it can't map a
  4 MiB SNES image the way the recompilation does; widescreen and translation
  want it too. Extracted assets are ROM-derived and stay out of the repository
  wherever they live (CLAUDE.md), so a user data directory beside the saves is
  the shape.
- **Open questions** in PLATFORM.md: battles playing out differently once
  timing changes, widescreen's exposed assumptions (the 0-255 object window at
  `$C0:A88D`, map data beyond VRAM, single-byte battle window positions), and
  GBA's constraints.
- **P1 leftovers**: `$217F` mirror writes still come from unported video code
  (the last of it in `$FD` is the location's tile animation loader `$FD:DE98`,
  which writes `$7F:0400` through the work RAM port that way; another is the
  battle's window quadrilateral rasterizer `$D1:F9E7`, which writes the window
  table through the work RAM port with 16-bit stores, so each edge's low byte
  also lands on APU port 3; RECOMP.md, "Phase 3, wave 1"),
  and the APU-direct backend's audio comparison misses one measure on
  `new-game` (PLATFORM.md, "P1 as built").
- **The battle's screen setup `$D1:F1B0`** (609 bytes, the largest unported run
  in `$D1`) sets the line interrupt's line, 211 as the field and the scenes do,
  which is `tm_set_line` now (the scene boot added it), and reads some 30 cells
  of the `$CD` frame work that aren't named yet.
- **The sound driver's waits on time and on the SPC700** (`core/sound_driver.c`):
  the boot's countdown at `$C7:011B` ($0800 passes, time for the SPC700 to start
  the driver) and every echo wait stay loops on the machine as idle entries.
  The Phase 4a port of the driver needs none of them -- there is no SPC700 to
  wait for once its program is C -- so these loops are deleted by that work
  rather than fixed by it.

## Timing without a beam

PLATFORM.md, "Timing without a beam", is the design: the census of every use of
the timing registers, what each is for, the native backend, and the rewrites
below. `python3 tools/timing_census.py` re-runs the census.

- **The native backend and the new calls.** Add `tm_wait_frame`,
  `tm_frame_time_left`, `tm_wait_line`, `tm_entropy` and `tm_odd_frame` to
  `timing.h`, and `in_ready` to `input.h`, each with a SNES backend that makes
  the original's accesses; then the native backend: a frame of vblank handler,
  main thread to its `tm_wait_frame`, line handler and picture, with
  `tm_enable_interrupts` and `tm_set_line` recording which handlers run and
  where, `VID_AT_LINE_HANDLER` resolved from the recorded line, and the
  acknowledges answering "pending" inside their own handler. The main thread
  runs as one coroutine through `runtime/port/fiber.h`: its frame waits are
  deep in its call stack (the field's `$C0:EC60` has 28 call sites, the
  battle's `$C1:007E` 20).
- **The event commands still on the machine** (RECOMP.md, "Phase 3a: event
  onto the field's objects"): 15 bridges, rows of the language's table that
  call out to code still the original's: letting an object behave as its state
  says (`$B0`, and `$AF` which runs it once; the field's state routines
  `$C0:9E29`, `$C0:A26B`, `$C0:A2CE`, `$C0:5B8D`, `$C0:3154`), the sound
  driver (`sound.c`), the menus' party routines (`$C2:8004`) and `$D6`'s
  carry, the tile copy `$C0:AF4E`, the location change from `$C0:3B71`, and
  `$4E`'s stack.
- **Bridges called from the engine are compared at the engine's boundary.**
  The engine calls a bridge directly, so shadow mode sees its call-outs as
  the engine's and abandons the bridge's own comparison ("started inside the
  engine's stretch"): `event_cmd_behave`'s 250,698 calls over the library are
  counted abandoned rather than matched, while the engine's entries match on
  every call. They are still checked (every call-out's registers, the
  engine's state at its end), and fuzzing covers them one by one; a way for
  shadow mode to compare a nested port's stretch would count them again.
- **Event's scaffolding, and what would let liveness drop it** (RECOMP.md,
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
    `$04C0` afterwards. If the menus' port establishes that function `$0B`
    ignores it, `DC_Y` holds at `$C0:9DC3` and the parse column goes, but only
    once `$C0:595C`/`$C0:597D` are ported too (they reach the handlers' entries
    as the original's code). The sound commands' calls to the driver keep Y
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
  - The menus' party routines 6, 7 and `$0A` (events `$F8`-`$FA`, `$D5`),
    through the dispatcher `$C2:8C36`: the menus' port names them.
  - The player's button assignments the scripts ask about, the first and the
    fourth (events `$30`/`$31`, `$3B`/`$3C`): which command each is (dash,
    confirm, ...) is the menus' settings screen's to say.
  Each is named for its address or its position until it is.
- **The field's shadows hold hardware bytes.** The tint (event `$F1`) is
  written as the fixed colour's register byte and a colour-math setting
  ($17 / $97), and the objects' sprite priorities are kept as OAM attribute
  bits: the field's interrupt hands them to the hardware as they are. The
  priorities are values (0-3) everywhere but their storage function
  (`field/object.c`, `object_set_sprite_priorities`); the tint is still
  written in the hardware's terms (`event/screen.c`, `tint_math`), until the
  field keeps it as a `VidColorMath` and converts it at its video calls.
- **The four windows' corner tiles** (`field/window_corners.c`) aren't known
  to be the party's windows: what they frame is still to be read.
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
- **The event scheduler `$C0:5A46` and the sprite frame queue `$C0:CA76`**:
  port them as "run the queued work while `tm_frame_time_left(limit)`". Name
  the event queue (`$0174`/`$0175`, linked through `$1080`) and the scripts'
  line limit (`$0169`, 72, set by `$C0:56A6`) in `re/ram.toml`, beside the
  animation updates' (`field_animation_line_limit`, 120, which event command
  `$47` sets). Shadow mode can disagree where the port's
  beam read and the original's fall on either side of a line boundary; keep
  the per-object work a call-out and measure it on the first run. Then run
  the routes with the native backend's drained budgets.
- **Frame waits to `tm_wait_frame`**: the races `$C0:3B71` and `$C0:4181`
  (`field/frame.c`); the attract's `$7E:31B0`/`$7E:31B7`
  (`attract/main_loop.c`, which reads `$4212` itself rather than through the
  service); the idle entries on flags a handler clears
  (`field_wait_for_frame_loop`, `menu_wait_for_frame_loop`,
  `menu_wait_ready_loop`, `battle_wait_for_frame_loop`,
  `battle_wait_for_nmi_loop`, `battle_wait_ca1f_loop`,
  `battle_wait_setup_counter_loop`, `scene_race_wait_for_tilemap_upload_loop`,
  `attract_wait_loop`); and the generated `$C0:0B28` and `$FD:EADA` (vblank
  with the handler off, then on), `$CD:044A`, `$CD:3E55`, `$CF:FCFB` and
  `$FD:EAEB`.
- **The line handlers' hblank waits go**: `field_irq_wait_hblank_loop`
  (`$C0:ECEB`), `battle_irq_wait_hblank_loop` (`$D1:FEB8`) and the scene
  boot's `$C3:0530`/`$C3:0538` exist to land the forced blank at the end of
  line 211, which `vid_raster_display` at the handler's line already says.
- **The controller-busy waits to `in_ready`**: `$C2:84EC` (`core/input.c`)
  and the scene handler's `$C3:0A8E` (`scene/frame.c`), which stop at the
  original's loop while the automatic read is busy. With these and the waits
  above rewritten, no caller of `tm_video_status` needs a native answer, and
  it becomes SNES-only.
- **The battle's wait for a line `$CD:3B82`** (`battle/wait.c`, which reads
  `$4212` and the counters directly): `tm_wait_line`. Its purpose isn't
  established: the frame work waits for line 64 before the screen script
  channels run while the timed effect slots do (`$CD:09AC`), and screen script
  command `$C1` (`$CD:1D93`, in no movie) waits for a line from the script.
  Neither is followed by a register write. Find out when `$CD:1609` is ported;
  if it is pacing, the wait goes.
- **The scene program's randomness and parity**: `$C3:02DD` mixes the beam
  counters into its seed and stays generated, with `$C3:01E4` (the noise
  effect, called from `$7E:6844`, and from bank C6's `$C6:E27E` through
  `$C3:000E`, which no movie runs) and `$7E:3443`. Port the mixing as game code
  over `tm_entropy`, validated by `--native on` rather than fuzzing (a port
  reads other counters, and the output is cosmetic). `$7E:9AB3` and
  `$7E:9EDF` read `$213F`'s field bit (the second through a 16-bit read of
  `$213E`) to move something every other frame: `tm_odd_frame`.
- **The timer resets.** `$C2:0043` past `$C2:00F2`, `$C2:940D` past
  `$C2:94B8`, `$FD:C000` and `$FD:EB13` clear `$4207-$420A` in a run of CPU
  register writes (where the menu ports stop). Nothing enables the timers after
  them, so natively they are nothing; for shadow mode a port needs a call that
  writes the zeros in the original's order, or the documented SNES escape.

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
    no call-outs. One stack stays, the game's main thread, which waits for the
    next frame from deep in its call stack (PLATFORM.md, "Timing without a
    beam"), so a finished build still needs one stack switch per frame, or
    Emscripten's asyncify around it. **So the third backend is not scheduled**
    (PLAN.md, "What this plan deprioritizes"), and when a browser build is
    wanted it is for that one coroutine.
  - **What the browser build is made of.** The default path generates C from the
    user's ROM at build time (RECOMP.md), and nothing ROM-derived can be shipped
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
- **zophar `chrono-7`** (everything maxed at the fair) is unused: its Lucca
  exhibit gate (`$7F:0055` bit 7) can be opened, but no movie needs it yet.
- **Coverage loop**: re-run `--misses` over the whole library after the latest
  movies and feed anything new back into the build.
- **Battle setup is never compared.** Every call of `battle_setup_state`
  (`$CC:E2E4`) and `battle_setup_graphics` (`$CC:ED87`) in the library is
  interrupted by NMI, so shadow mode counts them and compares none; the gate
  passes on them without looking. What checks them today is `--native on
  --recomp off` tracing identically to a known-good build (DISASSEMBLY.md,
  "The RAM map"). A fuzz state at their entry, or comparing interrupted calls
  when the interrupt touches none of their state, would close it.
- **Snes9x agreement tables** in RUNTIME.md were re-measured after the timing
  fix for `new-game`, `menu` and `field` only; `fair` and `fair-companion` got
  worse and are unexplained.

## Player and tooling

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
  is*, so porting them to the bar needs the resources typed first (RECOMP.md,
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
  (RECOMP.md lists them by name). Matching definitions properly, and splitting
  the column into registered entries, guards and the rest, would make the rest
  the number to drive to zero.
- **`tools/fuzz_routines.toml` has the scene's, the events' and the field's
  entries.** Each subsystem's Phase 3a rewrite adds its own from its PORTING.md
  rows as it goes; a check that every registered entry has one would say when
  the file is complete.
- **Scene buffers reached by address** (`grep -rn wram_ game/scene`): 32 uses
  in 7 files. The sequence scripts' bytes and the race's line-effect descriptor
  table are fixed parts of the program image at `$7E:3000` and could be named
  buffers of it; the scaler's rows, the bank `$FE` copies' destination, the
  title's plane tiles, the sprites' sources and lists and a store command's target are
  wherever the caller points, and want the caller ported to say what they are;
  the circle's table is the pilot's. The title loader's tile list at `$7F:0200`,
  which the original reads through the work RAM port, overlaps the race's scaler
  output (and the event variables), so the map can't name it in the scene's
  subsystem (`ram_map.py coverage --open`).
- **The scene loaders are fragments.** The title's graphics loader
  (`$7E:5978`-`$7E:5A00`) and the race's (`$7E:3DB3`-`$7E:3EB6`) are registered
  a block at a time (`scene_title_load_*`, `scene_race_load_*`), each named for
  what it puts in VRAM, because the loader around them calls the decompressor
  and the program's own routines (`$7E:3691`, `$7E:37A6`, `$7E:38AF`,
  `$7E:89ED`, `$7E:5E3C`). Porting each loader whole, as one routine that loads a
  list of resources to VRAM addresses, would retire twelve entries.
- **Scene work that is still the original's, beside its ports.** The title
  logo's sweep before it lands (`$7E:5CBA`, where `scene_title_move_logo`
  stops) and the ripple's new first offset each frame (`$7E:600E`, where
  `scene_title_ripple_columns` stops) are the program's code; porting them
  would end two stops mid-routine.
- **What scene's second review accepted as later work** (RECOMP.md, "Phase
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

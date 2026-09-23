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

- **Event's follow-up after its second review** (merged with these accepted;
  RECOMP.md, "Phase 3a: the event language, and its scaffolding"). Fix on
  main, checked by the gate:
  - refresh the `re/ram.toml` `source` lines that name functions `game/event/`
    no longer defines (`event_cmd_fight`, `event_cmd_tint`,
    `event_cmd_set_speed`, ...; about 17);
  - delete `FLOW_RESUME_AT`/`resume_at()` and `event_leave_y`;
  - `animation.c`'s `back_to_loop` and `interrupt_loop`: choose the mode, then
    `restart_animation`;
  - `calls.c`'s `finds_a_level` re-runs `return_from_function`'s search: bound
    the search, give it a "nothing to resume" outcome, and share `place_of`;
  - comment or remove the openings loop's restart from `field_current_object`
    (`engine.c:284`), and use one spelling of the object guard;
  - `party.c`'s `last_member_at_least` and its carry: one named decision;
  - `memory.c:94` reads the script through `wram_read8`: use the script
    accessor;
  - decode packed operands in the rows into typed parameters (a fade's
    brightness and rate, a tint's mix, a call's level and function,
    `Location`, a story flag's page and bit, the choice range; enums or bools
    for solid, blocking, arrival and control);
  - move `.routine` and `.bridge` out of the language's rows, and compile out
    `EventParameter`/`event_parameter()` with the scaffolding;
  - give `back()` and "keep the offset across the call" one home in
    `bridge.h`, where five files each have a copy;
  - operations take the parameters they read (explicitly, or a small struct per
    family), not the whole ~80-field `EventCommand`.
- **Field's follow-up after its review** (merged with these accepted;
  RECOMP.md, "Phase 3a: the field subsystem off the machine"). Fix on main,
  checked by the gate:
  - **`sprites.c`:**
    - a `SpriteRecord` type with storage functions, so `place_sprites` works
      on records rather than byte offsets;
    - drop `place_sprites`' `high_of`;
    - the table build loops on a local row, not the original's Y;
    - `bring_records_up_to_date` stops setting `cpu.x`;
    - choose C or a call by a step enum, not by comparing ROM addresses.
  - **`object_update.c`:** pass the object as a parameter, not through
    `field_current_object`, and mark `set_event_object_end`'s write as
    scaffolding.
  - **One bridge vocabulary in `bridge.h`**, replacing `JSR_SIZE` and the
    call-out helper copied into four files. One table per object-update step,
    in place of the parallel site arrays; `nmi.c`'s `uploads[]` should share
    `display.c`'s edge-run table.
  - **Duplicated definitions:** `link_or_none` means different things in two
    files, `TileAttributes` has two shapes, and `SPRITE_TABLE`,
    `LEAVE_ROW`/`JOIN_ROW` and `AXIS_X` are each defined twice.
  - **Dead or duplicated stores:** `motion.c`'s `stand_still` is overwritten,
    and `animation.c`'s `start_afresh` should call `object_restart_animation`.
    `nmi.c`'s three map-transition branches should be one table.
  - **`animation.c`'s frame-read half:** `object_current_frame` mutates under a
    reader's name, and callers compute ROM addresses for frame entries. It
    could replace the review skill's illustration once that is fixed.
  - **`object.h`:**
    - hide `FieldLink`'s encoding;
    - merge `object_in_view` and `object_marked_in_view`;
    - an enum for `ObjectRole.kind`;
    - `object_make_current` belongs in `bridge.h`.
  - **The two prediction guards** (`field_view_changes`,
    `every_draw_is_own`): accepted while shadow mode pairs a bridge's calls in
    order; restructure them if that pairing changes.
  - **Smaller:**
    - `line_effects.c`: `wave_part`'s return exists only for the wrapper, and
      `after_shading`'s fake `A = $FFFF`;
    - window widenings as literals, and layers as `0/1/2`;
    - staging buffers as raw addresses;
    - graphics banks `$D2-$D5` as a fixed bank layout;
    - `event_variables(g, 0xA8)`, which is window 2's table choice;
    - six stale `re/ram.toml` sources.
- **Coverage that field's rewrite thinned.** Inlining the object update's
  steps into their callers' time left more of their calls interrupted, and
  shadow mode compares none of those: `field_update_object_sprites` went from 78
  interrupted calls to 855, `field_draw_object_sprites` from 837 to 1713,
  `field_build_sprite_table` from 267 to 336 and `field_update_objects` from 6
  to 17. The fuzzer covers what shadow mode skips, except that
  `field_build_sprite_table` compares 0 fuzz states on either side (its draws run
  past the call-out budget), so it has shadow mode's uninterrupted calls and
  nothing else. A fuzz setup that bounds its draws would close that.
- **Shadow mode's call matching isn't written down where a porter looks.** A
  bridge's calls are matched to the original's in order and by site, which
  decides what a rewrite can inline: a step can be inlined only if it makes no
  call-outs itself, and draws from a shared site must all inline or none. The
  field rewrite learned it from 105923 mismatched draws. It belongs in
  PORTING.md's "Call-outs". Also from that rewrite: the mismatch report stops at
  16 details, and timing calibration rewrites other subsystems' rows, which each
  rewrite then reverts by hand.

- **What only the original's memory has, past the object tables.** The
  stray accesses are explicit and marked now (RECOMP.md, "Phase 3a: event onto
  the field's objects"), and a build without the original's memory has to
  decide each: the in-the-way search's slots past 64 (`motion.c`,
  `stray_slot_can_block`, which reads the next tables by their names), the
  sprite table's high part running on past its end (`sprites.c`, by address
  through `game/wram.h`), the fifth sprite's Y above the screen, the openings'
  clear of the first object's falling flag, and the row removal's in-view
  write. One is the game's: a command that names a party member without
  checking (`$07`, `$22`, `$95`, `$B6`) takes an empty place's marker as
  object 64, and the scripts do that (52,898 of `$95`'s calls over the
  library, a scene following a party of two), reading the tables' neighbours
  through the objects' accessors. The bounds check below would stop there.
- **Generated accessors don't check their index.** `vars_ram.c` computes
  `base + index * stride` with no bound, so slot 64 of a 64-slot table silently
  writes the next table. Field's row code did exactly that (`object_update.c`,
  unlinking with no neighbour lands on `field_object_in_view`), and its review
  found two more places with the same risk. It works only while storage is the
  original's layout. A bounds check in the generated backing, on in `ct_dev`
  and the fuzzer and off in the player, would make every such alias a loud
  failure over the library, before the storage moves.
- **Field's review deferred three platform gaps**: there is no sprite-list
  video service, so the 9-bit X, the `$E0` hide line and the screen-half
  wrapping sit in `game/field/sprites.c`, against the widescreen rule; the
  upload and line-effect services still take a per-call array of the original's
  write order; and `vid_set_line_effect` takes raw register bytes (the color
  math and window encodings in `line_effects.c`). These join the video service
  item under "Platform".

## Recompiler

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
- **The scene sequence interpreter's hand-back** (`scene/sequence.c`): a
  command the port doesn't handle is given back as a call-out to the loop from
  the entry's `JSR` site, which shadow mode compares at the loop's first pass,
  right only when no command ran before it (no movie has such a command).
  Stopping at the loop's head `$C3:0B03` instead is what shadow mode can now
  compare at the right pass, once the port puts back what the given-up command
  already wrote: the scratch cell `$F0` and the flags (RECOMP.md, "Between the
  waves: the validator").
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
  and operands named mechanically (`set_bc`, `party_routine_6`), and the 39
  command layouts read from handlers no port has checked yet.
  The sound driver's tables (found by the $C7 pilot, `core/sound_data.h`):
  `$C7:1871` is typed as one song's twelve three-byte entries, but the driver
  indexes 83 songs' worth (36 bytes each, to `$C7:241C`), and the two bytes per
  song at `$C7:241D-24C2` are still `mixed`.
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
  without being fields of a slot. Three more bytes the object scripts read,
  `$7E:1BA7` bit 4, `$1BAA` bit 5 and `$1BAF` bit 1, each stop the map screens'
  scroll from following the object and nothing says what else they mean, so
  `menu/object_script.c` names them in the file.
- **MD3**: every pointer symbolic, plus the static and dynamic shift tests.
- **MD4 residue**: 84 jump tables are bounded by layout alone, the claim that
  menu modes 8-10 and 15 are never selected is assumed, and C4 (RAM code) rests
  on the enumerated sections plus the miss report.
- **asar cross-check** of the generated source has never run (asar isn't
  installed here); the syntax points it would settle are listed in MATCHING.md.

## Platform

- **Ports feed the video service literal register bytes.** Scene's review
  found setup code building display state as `vid_mode_decode(0x07)`,
  `vid_set_screens(pf, at, 0x13, 0x04, ...)` and `vid_windows_decode(&w,
  field, 0x00)`: the decoders exist for bytes the game computes at run time,
  and a constant fed to one is the register value with a function around it.
  The ports should write the structs' fields and named constants
  (`VID_LAYER_BIT`, `.mode = 7`). Where there's no call for what a setup
  routine means (a whole display reset, `display.c`'s `init_video`, which is
  about 25 register-group writes), the service gains one. And uploads the
  service models (`vid_upload_oam`, `vid_upload_palette`, `vid_upload_vram`)
  still take a per-call array of the original's write order, which `game/`
  should never have to spell out.

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
    items are two, and they are no longer optional or later: **port Square's
    SPC700 driver to C** (it is interpreted assembly in the shipping path
    today, and `game/core/sound_driver.c` is only the CPU half that uploads it),
    and **a native mixer behind `au_*`** that decodes the same BRR samples and
    runs the same envelopes and echo, replacing `runtime/apu/dsp.c` in the
    shipped product. Both are PLAN.md's Phase 4a; `--audio direct` is the
    halfway house.
  - **The sound driver port's harness** (SOUND.md, "Validating the port"),
    before the port itself: a hook at the interpreted driver's tick boundary
    (its loop leaving the timer wait at `$029C`), a record of the commands and
    uploads it took that tick, and a comparison of the port's DSP writes and
    status with `--dsp-log`'s, per tick. Then a command-stream driver for the
    paths no movie reaches (commands `$88`, `$8E`, `$F4`, `$F5`, `$F8`, `$F9`,
    `$FD`, event `$F4`, the meter mode, zero-length slides), and a note-stream
    comparison for when the port drives the audio.
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
- **The event commands still on the machine** (RECOMP.md, "Phase 3a: the
  event language, and its scaffolding"): 36 bridges, rows of the language's
  table that call out to code still the original's. 20 are the moves
  (`movement.c`), calling the field's motion (`$C0:ABA2`, `$C0:5B95`,
  `$C0:ACFD`, `$C0:AC69`, `$C0:5614`, `$C0:568B`, `$C0:30B3`, `$C0:305D`) and
  its state routines (`$C0:9E29`, `$C0:A26B`, `$C0:A2CE`, `$C0:5B8D`,
  `$C0:3154`). `field`'s rewrite made the motion C over the game's state, in
  `field/object.h` (`direction_between`, `object_in_the_way`,
  `object_start_step`, `object_head`, `object_animate_movement`,
  `object_stand`, `object_line_up_with`, `object_line_up_with_tile`), and the
  animation commands (`event/animation.c`) can call `object_loop_animation`,
  `object_play_animation` and the rest instead of keeping their own copy of
  the modes. So the moves become operations: the carries they hand each other
  (`carry_after_tile_test`, `carry_after_next_to_test`) become return values,
  the A the facing lookup hands `$C0:5B95` goes, and their families -- to a
  tile, toward an object or a member, turning or keeping the facing, counted --
  one operation each. The rest wait on the sound driver (`sound.c`), the
  menus' party routines (`$C2:8004`), the tile copy `$C0:AF4E`, the location
  change from `$C0:3B71`, the facing aim `$C0:ABA2`, and `$4E`'s stack.
- **Bridges called from the engine are compared at the engine's boundary.**
  The engine calls a bridge directly, so shadow mode sees its call-outs as
  the engine's and abandons the bridge's own comparison ("started inside the
  engine's stretch"): per entry, 26,000-odd bridge calls over the library are
  now counted abandoned rather than matched, while the engine's entries match
  on every call. They are still checked (every call-out's registers, the
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
  without setting it first, so it starts from whatever event's calls (`$02`)
  or `$B5` last left there. A native build without event's scaffolding would
  start it elsewhere: the menus' port should give the phase a start of its own
  (a line or two of the wave, a frame).
- **What the scripts set that isn't established:**
  - `field_setting_1e` (events `$DE`/`$DF` raise it with the location they
    set; cleared at `$C0:2842` and `$C0:2866`, returned by `$C0:28A7`, ANDed
    at `$C0:7628`). The field's rewrite reads those routines.
  - `field_setting_bc` is the screen's shake (the field's rewrite found the
    layer scroll reading it; event `$F4` sets it); the name waits for the
    naming pass.
  - The menus' party routines 6, 7 and `$0A` (events `$F8`-`$FA`, `$D5`),
    through the dispatcher `$C2:8C36`: the menus' port names them.
  - The player's button assignments the scripts ask about, the first and the
    fourth (events `$30`/`$31`, `$3B`/`$3C`): which command each is (dash,
    confirm, ...) is the menus' settings screen's to say.
  Each is named for its address or its position until it is.
- **The field's shadows hold hardware bytes, so event writes them.** The tint
  (event `$F1`) is written as the fixed colour's register byte and a
  colour-math setting ($17 / $97), and the objects' sprite priorities as OAM
  attribute bits: the field's interrupt hands them to the hardware as they
  are. When the field keeps a tint and a sprite priority as values
  (`VidColorMath`, a priority 0-3) and converts them at its video calls, event
  writes those instead (`screen.c` `tint_math`, `properties.c`
  `set_priorities`).
- **One field-object vocabulary, after the field and event merges.** Event
  keeps local helpers for the field's objects (the list in RECOMP.md), and a
  review of the event rewrite found these definitions duplicated or at odds,
  which the pass that moves event onto field's object module settles:
  - `field_requests`: `TILE_COPY_QUEUED = 0x20` (`screen.c`) and
    `BATTLE_STARTING = 0x21` (`locations.c`, bit 5 with the battle bit 0), and
    `LOCATION_CHANGE = 0x80`; one set of request bits for the field.
  - Bit 7 of an object's state and of its step length: `DISABLED` (`calls.c`,
    `facing.c`, `movement.c`) and `CALLS_SKIP` (`properties.c`) name the same
    kind of flag three ways.
  - The sprite flag: `NO_SPRITE_SHOWN` (`calls.c`) and
    `SPRITE_HIDDEN`/`SPRITE_SHOWN`/`NO_SPRITE` (`properties.c`).
  - "Well on screen" twice with different margins: `flow.c` (the object's
    tile, 2-13 across and 2-12 down) and `movement.c` (the running object's
    own tile as aimed, the same bounds) -- one test on field's screen.
  - Facing as `uint8_t` 0-3 (`Facing` in `facing.c`) where the field will
    have an enum; the animation modes (`AnimationMode`, `animation.c`); the
    arrival and blocking bits (`movement.c`, `properties.c`).
  - The accessor names shaped by the map's layout
    (`field_object_animation_mode_mode`, `field_object_move_state_animation_count`,
    `field_object_move_state_tile_mode`, `field_object_state_state`), in the
    map's naming pass.
- **What the field's rewrite left** (RECOMP.md, "Phase 3a: the field subsystem
  off the machine").
  - The map's stuttering names for the object tables' fields
    (`field_object_animation_mode_mode`, `field_object_move_state_animation_count`,
    `field_object_state_state`) and `field_setting_bc`, which is the screen's
    shake (the layer scroll reads it; the map says so now); event `$F4`,
    `set_bc` in `tools/disasm/eventscript.py` and `event_cmd_set_bc` want that
    name too. A naming pass over the map, not the ports.
  - A sprite service in `runtime/platform/`: `field/sprites.c` writes the OAM
    image through the work RAM port, whose cursor the original leaves, where a
    sprite list handed to the platform would do; the line effects likewise
    hand the video service register bytes (`field/line_effects.c`) and the
    transfers' write order (`steps`) rather than `Vid*` fields.
  - `object_play_animation` counts whole passes; event `$B7` starts a one-off
    with an odd countdown (its operand plus one, in the half passes the
    storage keeps), which the operation can't express yet. When event moves
    onto `field/object.h`, it wants a form that takes the half pass.
  - The four windows' corner tiles (`field/window_corners.c`) aren't known to
    be the party's windows: what they frame is still to be read.
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
  `battle_wait_setup_counter_loop`, `scene_wait_39_clear_loop`,
  `attract_wait_loop`); and the generated `$C0:0B28` and `$FD:EADA` (vblank
  with the handler off, then on), `$CD:044A`, `$CD:3E55`, `$CF:FCFB` and
  `$FD:EAEB`.
- **The line handlers' hblank waits go**: `field_irq_wait_hblank_loop`
  (`$C0:ECEB`), `battle_irq_wait_hblank_loop` (`$D1:FEB8`) and the scene
  boot's `$C3:0530`/`$C3:0538` exist to land the forced blank at the end of
  line 211, which `vid_raster_display` at the handler's line already says.
- **The controller-busy waits to `in_ready`**: `$C2:84EC` (`core/input.c`)
  and the scene handler's `$C3:0A8E` (`scene/nmi.c`), which stop at the
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
  and counts guards and entry wrappers with everything else, so "54% on the
  machine" for scene reads as a backlog when it is entries, guards and bridges
  (RECOMP.md lists them by name). Matching definitions properly, and splitting
  the column into registered entries, guards and the rest, would make the rest
  the number to drive to zero.
- **`tools/fuzz_routines.toml` has the scene's, the events' and the field's
  entries.** Each subsystem's Phase 3a rewrite adds its own from its PORTING.md
  rows as it goes; a check that every registered entry has one would say when
  the file is complete.
- **The scene sprite writers accept inputs they diverge on.** With the entry
  offset or the high-table pointer past `$2000`, the named sprite table writes
  bank `$7E` where the original, through a data bank of 0, reaches I/O space
  (19569 of 20000 fuzzed states against the predecessor's 22). No game state
  points them there -- the lists always write the OAM buffer at `$0700` -- and
  the loose fuzz runs were narrowed to low RAM, recorded in PORTING.md and
  RECOMP.md but not in the port. A guard refusing a cursor that could run past
  low RAM would make the four entries exact on every input they accept
  (`game/scene/sprites.c`, `scene_load_sprite_list` and its three siblings).
- **Scene buffers reached by address** (`grep -rn wram_ game/scene`): 22 uses,
  18 of them from the Phase 3a rewrite. The sequence scripts' bytes and the
  race's line-effect descriptor table are fixed parts of the program image at
  `$7E:3000` and could be named buffers of it; the scaler's rows, the bank `$FE`
  copies' destination, the tile rows `$7E:5E54` reads, a task's next-step cell
  and the sprites' sources are wherever the caller points, and want the caller
  ported to say what they are. The circle's table is the pilot's.
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

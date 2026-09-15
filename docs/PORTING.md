# Porting routines

Ported code replaces original routines one at a time while the rest of the game
keeps running on the interpreter. Each port is validated call by call against
the original, independent of timing.

## Layout

Game code is organized by subsystem, the way the game's own programmers would
have split it, not by where the routines ended up in the ROM. The ROM address
is recorded in each routine's comment. Names use the subsystem as a prefix
(`field_nmi`, `menu_wait_for_frame_loop`). When porting a routine, place it
by what it does; move routines when their purpose becomes clearer.

| Path | What |
|---|---|
| `game/core/` | System code shared by every mode (display and interrupt control) |
| `game/data/` | Data handling (the decompressor) |
| `game/event/` | Event scripts: the engine loops and command handlers, grouped by kind (`flow.c`: jumps, conditionals, waits; `calls.c`: calling functions in other objects; `dialog.c`: dialog boxes; `animation.c`: object animation; `input.c`: input conditions; `movement.c`, `facing.c`, `properties.c`: objects; `variables.c`: object data into variables; `arithmetic.c`: variable arithmetic; `memory.c`: memory and pointers; `sound.c`, `screen.c`: sound and screen effects; `locations.c`: location changes) |
| `game/field/` | Field mode: interrupt handlers, frame sync, objects and their motion |
| `game/menu/` | Menu mode: frame sync |
| `game/battle/` | Battle mode: display synchronization, the NMI handler, drawing battle objects' sprites, scroll tables, timers, per-frame work buffers, the window tilemap and combatant animations (so far) |
| `game/attract/` | Attract sequence and title screen code the game runs from RAM |
| `game/registry.c` | Table registering each routine: address, name, entry precondition on P, don't-care set, and time cost in ON mode |
| `runtime/port/machine.h` | Helpers for touching the machine the way the original does: bus reads and writes, stack, returns, flags |
| `runtime/port/native.c` | Registry, interception, call-outs, shadow validation, reporting |
| `runtime/port/fiber.c` | Coroutines native routines run on, so call-outs can suspend them |

## Modes

```sh
build/main/ct_dev --rom roms/ct-usa.sfc --frames 7200 --movie oracle/movies/new-game.ctm --native shadow
build/main/ct_dev --rom roms/ct-usa.sfc --frames 7200 --movie oracle/movies/new-game.ctm --native on
```

- **`off`:** the interpreter runs everything.
- **`shadow`:** the interpreter still runs everything. On each entry to a
  registered routine, the machine is cloned and the native routine runs on the
  clone. When the original returns, the two machines must match exactly:
  registers and flags, WRAM (including stack bytes), SRAM, VRAM, CGRAM, OAM,
  PPU and CPU I/O register state, and the CPU-to-APU ports. Timing isn't
  compared, and neither is state that changes with time on its own: HDMA's
  per-channel line counters and table addresses, and the PPU registers enabled
  HDMA channels write. Calls interrupted by NMI/IRQ are counted but not
  compared. Registers and flags in the routine's don't-care set (below) aren't
  compared. Any mismatch prints the first differing field and makes `ct_dev`
  exit nonzero.
- **Idle entries** stand in for one pass of a busy-wait loop (the game's
  wait-for-next-frame loops) instead of a routine. The native function runs
  the loop body and either leaves PC at the loop head or sets it to the loop's
  exit. In `on` mode a waiting pass skips ahead to the next point where
  `$4212`'s blanking bits can change (hblank end, hblank start, or the end of
  the line), so interrupts still land and loops polling those bits still see
  every change. `shadow` mode skips idle entries.
- **Guards** cover code the game decompresses or copies into RAM, where the
  same address holds different code and data at different times. An entry's
  `guard` checks, with `m_code_matches`, that the bytes at the entry hash to
  the code the port was written for. Only the 64-bit hash is kept, never the
  code. If the guard fails, the interpreter runs whatever is there.
- **`on`:** registered routines run natively in place of the original. A native
  routine finishes instantly, so the time the original would have taken is
  charged afterwards, one scanline at a time, so frame boundaries and line
  events still happen. Each call's cost is its own bus-access clocks times the
  routine's `cost_milli`/1000, so cost tracks data size.

## Call-outs

A port calls code that isn't ported yet with `m_jsr`/`m_jsl`, giving the
original call site. The call doesn't run inside the native routine. The
routine runs on its own coroutine (`runtime/port/fiber.c`); a call-out pushes
the same return address the JSR/JSL would, points PC at the callee, and
suspends. The system loop then runs the callee like any other code: time
passes, interrupts and frames happen, and a ported callee runs natively. When
the callee returns to the call's return address with the stack as it was, the
routine resumes. So a port may call code that waits for frames (event command
handlers, fades), and ports can nest.

In `shadow` mode the native routine runs on the clone up to its first
call-out. The machines are compared when the original reaches the same call
(same target and stack pointer), as well as at the return. While the callee
runs, only the original machine moves. When it returns, the clone is reloaded
from it, and the native routine continues to the next call or its return. A
port is thus checked at every call it makes, not just at the end. That makes
`shadow` strict about registers passed to callees; `m_jsr_dc`/`m_jsl_dc` name
registers and flags a callee doesn't read. An interrupt in the stretch before a
checkpoint skips that one comparison, and later stretches are still checked.

If a callee unwinds past its return address (discarding its caller's frame),
the native routine is abandoned, since the original's continuation never runs
either. The report counts these.

A port doesn't have to run to the end. It can stop at any instruction boundary
in the original by setting PC (and PB) there and returning. The interpreter
carries on from there, and `shadow` checks that the original reaches that
point with the same stack pointer. That covers routines that end by jumping
rather than returning, and states a port doesn't handle yet.

Save states can't capture a routine suspended in a call-out; `ct_dev` warns
when `--save-state` is used while one is.

## Writing a port

1. Refresh the analysis with runtime call edges, so liveness knows every caller:

   ```sh
   build/main/ct_dev --rom roms/ct-usa.sfc --frames 7200 --movie oracle/movies/new-game.ctm        --coverage traces/coverage.txt --calls traces/calls.txt
   python3 -m tools.disasm --coverage traces/coverage.txt --calls traces/calls.txt
   ```

2. Read the original with `python3 -m tools.disasm.linear BB:AAAA` and the
   listing. Check `disasm/out/functions.json` for the M/X widths it's entered
   with, and `disasm/out/liveness.json` for `dead_after_return`: the registers
   and flags no caller reads after it returns.
3. Write readable C with the same observable effects: memory writes (including
   stack bytes from push/pull), I/O writes, and every register and flag that
   isn't dead after return. Use the data bank and direct page as the original
   does (`m_abs`, `m_dp`), not hard-coded banks. Call routines that aren't
   ported yet with `m_jsr`/`m_jsl`, giving the original call site. Registers
   and flags must be exactly what the original has at that point (see
   Call-outs).
4. Register it in `game/registry.c` with its entry precondition (for example
   8-bit A and binary mode) and its don't-care set, so calls under other
   conditions fall back to the interpreter. A guard function must only peek at
   memory: it runs on the real machine, and a bus read there costs time and
   sets open bus. A guard that read `$CD02` through the bus made shadow runs of
   the fight drift from interpreter-only runs within a few hundred frames; with
   peeks the two produce identical traces.
5. Run `--native shadow` over the movie library. Every call must match. Then
   run `--native on` and compare frames with the interpreter-only run.
6. Fuzz it with `ct_fuzz` (below), especially paths the movies don't reach.
7. Set `cost_milli` from the `original/native-bus` ratio `shadow` reports.

Liveness is only as good as the call graph. Functions called from code the
analysis can't see (RAM code, unresolved indirect jumps) keep everything live,
which is why step 1 feeds in the runtime call log. Rerun it when movies reach
new parts of the game, before relaxing a routine further.

## Fuzzing

Shadow mode only checks the states the movies reach. `ct_fuzz` checks a
registered routine against the original on random states:

```sh
build/main/ct_dev --rom roms/ct-usa.sfc --frames 11400 --movie oracle/movies/field.ctm --save-state traces/field.state
build/main/ct_fuzz --rom roms/ct-usa.sfc --load-state traces/field.state --routine event_cmd_if_var_const \
  --iterations 50000 --range Y=0000-7FFF
```

Each iteration randomizes A, X, Y, the arithmetic flags and all of work RAM
except the stack page, applies the entry precondition, and calls the routine
as a JSR would (`--long` for JSL, `--interrupt` for interrupt handlers, which return with RTI). The original runs on the interpreter, and the
native routine on a copy with call-outs interpreted in place. The machines are
compared where the native routine stopped: normally the return, or the
instruction where it handed back to the interpreter. States where the native
routine's callees together don't finish within `--step-limit` instructions, or
the original doesn't within four times that (it also runs what the native
routine did itself), are skipped: random RAM sends some callees into endless loops. The
native routine runs on its own fiber and is abandoned when that happens, since
what an unfinished callee leaves behind can keep a native loop from ending.

Keep random values inside what the game can produce when the difference
matters. Past the end of a data area, indexed reads reach I/O space, where open
bus holds the last byte on the data bus. The interpreter's instruction fetches
set it and native code doesn't, so those reads differ in ways real game data
never exercises. `--range` limits a register (script offsets, for example), and
`--keep` leaves a WRAM range as it is in the base state. `--fill BBAAAA-BBAAAA=lo-hi` gives a WRAM range random bytes within limits, for example a pointer's bank byte in script operands.

Event commands index object tables by the current object's slot in `$6D`; a
random slot reaches I/O space the same way, so fuzz them with
`--keep 7E006D-7E006E` as well.

Passing so far: the event control flow commands (50000 states each, two
seeds, `--range Y=0000-7FFF`), the event movement commands (10000 each, also
keeping `$6D`), `display_blank_and_stop_interrupts`,
`field_install_nmi`, `field_install_irq` and `field_update_object_visibility`
(20000 each). The three 16-bit and variable-vs-variable conditionals
(`$13`-`$15`), which no movie reaches yet, are validated only this way. Not
fuzzed: `decompress`, whose random source and destination pointers make it
overwrite its own stack and variables (it needs generated streams), and
`field_nmi`, which returns with RTI.

## Status

| Routine | Address | Calls in new-game movie |
|---|---|---|
| `display_blank_and_stop_interrupts` | `$C0:0B4E` | 17, all match |
| `field_install_nmi` | `$C0:0B64` | 10, all match |
| `field_install_irq` | `$C0:0B75` | 10, all match |
| `event_run_init_scripts` | `$C0:58DE` → `$C0:9DC3` | runs at map load; all calls match in every movie (7-10 per movie). Event command handlers, some waiting for frames, run as call-outs |
| `event_run_object_script` | `$C0:5A93` | the per-frame, per-object event script executor: 21719 calls in new-game, 59580 in fair-companion, all match; X, N, Z, C don't-care. One call per movie is abandoned: a handler unwinds the executor's frame, as the original does. The scheduler that calls it (`$C0:5A46`) stops when the beam passes a scanline limit, so which objects run in a frame depends on timing; it stays on the interpreter |
| Event control flow: `event_cmd_jump_forward` `$10`, `jump_back` `$11`, `if_var_const` `$12`, `if_var_const16` `$13`, `if_var_var` `$14`, `if_var_var16` `$15`, `if_mem_const` `$16`, `wait` `$AD`, `yield` `$B1`, `halt` `$B2` | `$C0:62B5` … `$C0:3543` | all calls match in every movie (fair-companion: `halt` 33101, `jump_back` 7789, `if_mem_const` 5288, `if_var_const` 3837, `yield` 2382, `wait` 1148). `$13`-`$15` aren't reached by any movie; all ten pass 50000 fuzzed states on two seeds. Conditionals with an operator outside the tables hand back to the original at the operator call |
| Event movement: `event_cmd_move_toward_party_member_until_on_screen` `$8F`, `move_toward_object` `$94`, `move_toward_party_member` `$95`, `move_to_tile` `$96`, `move_toward_object_counted` `$98`, `move_toward_party_member_counted` `$99`, `move_by_variables` `$9D`, `move_toward_object_keep_facing` `$9E`, `move_toward_party_member_keep_facing` `$9F`, `run_state_routine` `$B0`, `follow_object` `$B5`, `follow_party_member` `$B6` | `$C0:3002` … `$C0:5429` | all calls match in every movie (fair-companion: `follow_party_member` 14239, `run_state_routine` 9784, `move_to_tile` 3382, `follow_object` 2503). The movement primitives they call stay in the original as call-outs. `$8F`, `$98`, `$99`, `$9D`-`$9F` aren't reached by any movie; all twelve pass 10000 fuzzed states |
| Event locations (`locations.c`): `change_location` `$E0`, `change_location_from_variables` `$E2`, `set_location_0a` `$DC`, `set_location_00` `$DD`/`_with_1e` `$DE`, `change_location_now` `$E1`/`_with_1e` `$DF` (stores the location and hands the vblank wait and field restart to the original), `start_battle` `$D8`, `remove_from_party` `$D6`, `party_routine_0a` `$D5` | `$C0:3A07` … `$C0:3B9E` | `change_location` (13 calls) and `start_battle` (2 calls in `gato-battle`) are reached by the movies, all matching; all ten pass 20000 fuzzed states |
| Event sound and screen: `play_sound` `$E8`, `play_song` `$EA`, `sound_81_83` `$EB`, `sound_command` `$EC`, `wait_apu_port1` `$ED`, `wait_apu_port3` `$EE` (`sound.c`; the sound driver at `$C7:0004` is called out); `brightness` `$F0`, `wait_brightness` `$F2`, `fixed_color` `$F1`, `wait_color` `$F3`, `set_bc` `$F4`, `set_1f` `$E3`, `copy_tiles` `$E4`, `queue_tile_copy` `$E5`, `set_layer_scroll` `$E6`, `scroll_screen` `$E7`, `party_routine_6_7`/`_6`/`_7` `$F8`-`$FA` (`screen.c`) | `$C0:3C05` … `$C0:3E75` | all calls match in every movie (fair-companion: `wait_apu_port3` 98, `play_song` 10); all 19 pass 5000 fuzzed states. The APU port waits read the ports directly and still match in shadow mode. Brightness and fixed color were identified from the field NMI port, which copies `$19` to `$2100` and `$21` to `$2132` |
| Event variables and memory: assignments, copies, arithmetic, bit operations, shifts and the random table (`$47`, `$4F`-`$52`, `$5B`-`$61`, `$63`, `$64`, `$67`, `$69`, `$6B`, `$6F`, `$71`-`$73`, `$75`-`$77`, `$7F`; `arithmetic.c`); reads and writes through script pointers, the `$7F:0000` memory area, memory bit operations and the MVN data copy (`$48`-`$4E`, `$53`-`$56`, `$58`-`$5A`, `$65`, `$66`; `memory.c`); latched input bits of `$51` (`$40`-`$44`; `input.c`) | `$C0:6740` … `$C0:6D09` | all calls match in every movie, though each is reached at most a few dozen times; all 45 pass 20000 fuzzed states (the data copy 3000). Pointer commands are fuzzed with `--range Y=0000-0000 --fill 7F2004-7F2004=7E-7F` so the pointer lands in WRAM |
| Event object properties and more movement: sprite flags `show_sprite` `$90`, `hide_sprite` `$91`, `set_sprite_flag_80` `$7E`, `show_object_sprite`/`hide_object_sprite` `$7C`/`$7D`; `set_1b01` `$84`, `set_step_count` `$87`, `set_1a00` `$89`/`_from_variable` `$8A`; positions `set_tile_position` `$8B`/`_from_variables` `$8C`, `set_position` `$8D`; `set_sprite_priority` `$8E` (`properties.c`); `move_in_direction` `$92`/`_keep_facing` `$9C`, `move_to_tile_from_variables` `$97`, `move_to_tile_counted` `$9A`, `move_to_tile_keep_facing` `$A0`/`_from_variables_keep_facing` `$A1`, `move_to_tile_with_velocity` `$7A` (uses the hardware multiplier and divider), `move_with_velocity` `$7B` (`movement.c`) | `$C0:4B49` … `$C0:5191` | all calls match in every movie (fair-companion: `set_1a00_from_variable` 124, `set_tile_position` 54, `move_to_tile_keep_facing` 53); all 20 pass 20000 fuzzed states. The move-to-tile commands share one port parameterized by each code copy's call sites |
| Event dialog, animation and waits: `set_dialog_table` `$B8`, `dialog` `$BB`, `dialog_top` `$C1`, `dialog_bottom` `$C2`, `choice` `$C0`, `choice_top` `$C3`, `choice_bottom` `$C4` (`dialog.c`); `loop_animation` `$AA`, `play_animation` `$AB`, `static_frame` `$AC`, `reset_animation` `$AE`, `loop_animation_0`/`_1` `$B3`/`$B4`, `play_animation_for` `$B7` (`animation.c`); `wait_4` `$B9`, `wait_8` `$BA`, `wait_16` `$BC`, `wait_32` `$BD`, `run_state_routine_once` `$AF` (`flow.c`) | `$C0:2E67` … `$C0:36DD` | all calls match in every movie (fair-companion: `dialog` 718, `play_animation_for` 265, `choice` 124); all 19 pass 20000 fuzzed states. Top and bottom box positions were confirmed from new-game movie screenshots |
| Event calls: `return` `$00`, `call` `$02`, `call_when_accepted` `$03`, `call_and_wait` `$04`, party member versions `$05`-`$07`, `refuse_calls` `$08`, `accept_calls` `$09`, `disable_object` `$0A`, `set_object_flag`/`clear_object_flag` `$0B`/`$0C`, `set_1c80` `$0D`, `set_arrival_flags` `$0E` (`calls.c`) | `$C0:5F74` … `$C0:62A4` | all calls match in every movie, though the movies reach them only a few dozen times. All pass 30000 fuzzed states, and the call and return commands another 30000 with the saved-position table (`$7F:0580-$7F:0DFF`) kept from the base state, so its empty slots exercise queueing and the return's level search |
| Event conditions, facing and variables: `if_mem0_below` `$18`, `if_object_value` `$1A`, `if_object_in_view` `$27`, `if_object_on_screen` `$28`; input conditions `$2D`, `$30`, `$31`, `$34`-`$39`, `$3B`, `$3C`, `$3F` (`input.c`); facing `$0F`, `$17`, `$1B`, `$1D`, `$1E`, `$1F`, `$25`, `$26`, `$A6`-`$A9` (`facing.c`); `set_object_value_from_var` `$19`, `_from_memory` `$1C`, `set_var_from_2980` `$20`, `get_object_tile` `$21`, `get_party_member_tile` `$22`, `get_object_facing` `$23`, `get_party_member_facing` `$24` (`variables.c`) | `$C0:5535` … `$C0:6732` | all calls match in every movie (`get_object_tile` 2978, `get_party_member_tile` 1868 in fair-companion); most aren't reached by the movies. All 35 pass 20000 fuzzed states. The input cells were identified by running the field movie and comparing with its buttons |
| Battle menu window and number sprites: `battle_draw_menu_window` `$C1:0C2D`, `battle_place_menu_cursors` `$C1:17DD` (`battle/menu_window.c`); `battle_script_build_number_sprites` `$C1:5ADB` (`battle/numbers.c`) | | the menu window redraw: page 0 rebuilds the window through `$C1:1C3A`/`$C1:0299` or redraws the party status; page 1 lays out the tech list, dims techs whose cost bytes exceed the members' `$5E34` or whose partners are missing, and writes the members' values and the selected tech's costs as digits (the local `$C1:0EE1`, `$C1:0872`, `$C1:0EBA`, `$C1:103E` and `$C1:011A` are inlined with their stack bytes; `$C1:0A88`, `$C1:1115`, the digit helpers and the bank CD/CF routines are call-outs). The cursor routine places the four cursor sprites at `$0700` on targets, on a combination tech's members or at the list position. `$C1:5ADB` handles animation script opcodes `$50`-`$55` (reached through `JSR ($7A6B,X)`): up to four digit sprites per combatant at `$922D` from `$AD9C` (digits through `$CF:FDAD`), continuing through the shared handler tail `$C1:75BB`, ported inline. Only `gato-battle` reaches them: all calls match (449, 448, 472). All three pass 20000 fuzzed states, plus 20000 more per branch group with `--fill` pinning `$95DB`, `$9609`, `$9613`, `$95D5` or `$A3D1` (the menu window skips up to 8% of states, where unported callees loop on random RAM) |
| Battle main loop (`battle/main_loop.c`): `battle_main_loop_start` `$C1:8000`, `battle_main_loop` `$C1:8120`, `battle_main_loop_body` `$C1:812C`, `battle_main_loop_tail` `$C1:8345`, `battle_run_queued_scripts` `$C1:8461` | | the loop the battle runs from its JML to `$C1:8000` until it jumps to `$C1:0006`. It never returns, so it is split into entries that each stop where the next takes over: the setup (`$C1:8006`, which orders `$B163` and `$B23A` by indices `$C1:AF22` picks) stops at the loop head `$C1:8120`; the head counts `$0027` and calls battle work 3 (`$C1:0003`, which waits for frames) and stops at its return `$C1:812C`, so the body, which starts after the wait, can be fuzzed; the body (the combatant updates, the status flag calls at `$C1:81B6`, and the exits taken when `$24` is set, when no enemy slot is left, or with `$99CD`) stops at the timer entries `$C1:8231` or `$C1:8345`; the tail stops at `$C1:8120` again or leaves through `$C1:83BB`/`$C1:8423` and stops at `$C1:0006`. The debug path at `$C1:8356`, reached only when the ROM byte at `$C1:FFFF` is set (it is 0), stays in the original. `$C1:8461` is the thirteenth timer kind's handler, which also runs from the loop while `$AF25` is set: it runs the script commands of the queued enemies from bank CC through the handler table at `$C1:B80D`, as call-outs; its local routines `$C1:883D` and `$C1:8862` are inlined with their stack bytes. All calls in `gato-battle` match (1, 172, 172, 171, 42); the other movies don't reach the battle. The body passes 50000 fuzzed states and `$C1:8461` 50000 (two seeds), the tail 20000. The setup and the head can't be fuzzed: their first call-outs wait for frames |
| Battle window and combatant animations: `battle_draw_window` `$C1:0299` (`battle/window.c`); `battle_step_combatant_animations` `$C1:2D9F` (`battle/animation.c`) | | redrawing the whole window tilemap at `$0CC0` for the layout in `$9F20`: blank tiles, corners, each party member's name (`$9412`), HP, maximum HP and MP digits and gauge, eleven-tile entries from `$ACBC`, and while `$A6DE` is set the members' 7x6 windows (`$CC:FA41`/`$FA6B`), their frames copied from `$D1:59FC` into `$0B40`, attributes and cursor tiles. The local routines `$C1:0785`, `$081E` (with its fall-through into `$0872`), `$08E8` and `$0929` are ported inline with their JSR stack bytes. The other routine steps three combatants' animations each frame: the party, or the enemy group `$A4` selects, in an order rotated by the `$9922` flags. Delays count down, and the next frame comes from the pose's list in bank `$E4` (party) or from the lists an enemy copies into `$97D9`/`$9799` through the long pointers at `$C0`/`$C3` when its animation changes. The party's turn toward its target (`$C1:2F22`) is inline. The digit, gauge, pose (`$C1:2F97`), multiply and graphics (`$C1:1C4A`) routines are called out as native entries; the divide `$C1:00D7`, the direction `$C1:0222`, the dispatcher `$C1:0045` and the pose handlers `$C1:308C` (shared with `$C1:305C`) stay in the original. All calls in `gato-battle` match (9, 172); the other movies don't reach them. The window passes 45000 fuzzed states on two seeds (about 4% skipped where random callees don't return). The animation step passes 60000 on two seeds with the frame list offsets `$1C99-$1CDA` kept: a list starting with 0 makes the frame search loop forever, in the original too, and a native loop that doesn't yield hangs the fuzzer. It passes 60000 more with the delays `$9689-$968B` and `$9694-$969B` filled with 1, so that each combatant takes a frame |
| `battle_nmi` `$D1:E77C` (`battle/nmi.c`) | | the battle NMI handler: all 2054 calls in `gato-battle` match; 456 full-body fuzzed states match (`--interrupt --keep 7E007F-7E007F`; the rest skipped where the unported per-frame callees loop on random RAM). Pause and frame-advance input paths are covered only by fuzzing |
| Battle object sprites (`battle/sprites.c`): `battle_draw_objects` `$CF:E83A`, `battle_draw_object_underlays` `$CF:EDB8`, `battle_copy_sprite_tiles` `$CF:EB94`, `battle_draw_sprite_parts` `$CF:EAC5`, `battle_draw_object_overlays` `$CF:EDF7`, `battle_draw_shadow` `$CF:EA8A`, `battle_step_object_effect` `$CF:E92C`, `battle_step_prepared_sprites` `$CF:E781` | | the per-frame pass that sorts battle objects by depth and writes their sprites to the OAM buffer, the hottest non-waiting code in the fight. All calls in `gato-battle` match (`draw_objects` 1985, each per-object routine 5955, `step_prepared_sprites` 1353); `step_object_effect` isn't reached. All eight pass 30000 fuzzed states. The effect routines they call (`$CF:EEEB`, `$CF:E99C`) stay in the original |
| Combatant update (`battle/stats.c`): `battle_update_combatant` `$C1:B279` | | the common part of the per-combatant update run for every slot each frame; when a combatant is knocked out it stops at `$C1:B2B1` (or `$C1:B2B9`), where the original handles the rare knockout path with its eight call-outs. All 2391 calls in `gato-battle` match; 30000 fuzzed states pass |
| Combination tech members (`battle/stats.c`): `battle_update_combo_members` `$C1:B575` | | filling the 3x3 pairing table at `$B3EB` from the members' pairing nibbles and flagging, through `$FD:A8A5` (a call-out), who can join a combination tech. All 106 calls in `gato-battle` match; 20000 fuzzed states pass |
| Font glyphs (`menu/glyphs.c`): `menu_draw_glyph` `$C2:5E36`, `menu_draw_glyph_pixel` `$C2:5E89` | | drawing a glyph's two bitplanes into tile graphics pixel by pixel, in one of eight packings chosen by `$6A` (the handler table's `JMP ($5EAF,X)` is ported as a switch, with a guard, which only peeks, that leaves out-of-range modes to the original). All calls in `gato-battle` match (122, 2928); both pass 30000 fuzzed states with the destination pointer kept in `$7F` and the layout index small, since otherwise random offsets carry writes into I/O space or the stack page. The menu code switches stacks from an interrupt, sometimes in the middle of a glyph; shadow mode used to call that leaving the routine, and now counts it as interrupted when an interrupt was taken during the comparison |
| Orbits, sine scaling, object state reset and the setup wait: `battle_step_orbits` `$CF:F978`, `battle_sine_scale` `$CF:F9FB` (`battle/sprites.c`); `battle_reset_object_state` `$CC:F06B` (`battle/buffers.c`); `battle_wait_setup_counter_loop` `$CC:F05B` (`battle/wait.c`) | | up to eight objects moving in circles (radius times sine and cosine from the table at `$C0:F900`, through the multiplier), the per-battle reset of object state, and an idle entry for the busy wait at the end of battle setup (`$CC:ED87`), which had been the largest block of interpreted instructions left in the fight. All calls in `gato-battle` match (612, 140, 16); the three routines pass 30000 fuzzed states |
| Record groups and bitmap sprites: `battle_index_record_groups` `$C1:48EC`, `battle_build_bitmap_sprites` `$C1:4943` (`battle/sprites.c`) | | indexing 16 groups of 4-byte records in bank CE, and building sprite entries at `$4500` for up to four shapes from per-row bitmasks in the `$2D00` stream. All calls in `gato-battle` match (8, 64); the indexer passes 20000 fuzzed states. The sprite builder can't be fuzzed: it starts by waiting for the next frame, which never comes without interrupts |
| Battle window status: `battle_draw_party_status` `$C1:05A7`, `battle_number_to_2_digits` `$C1:0174`, `battle_blank_leading_zeros` `$C1:104E` (`battle/gauges.c`) | | each frame, the next party member's HP (three digits, highlighted when 0 or at most an eighth of the maximum on blink frames) and MP (two digits) in the window tilemap, laid out by `$9F20`, then the gauges; its digit helpers are reached as call-outs, most of them native. All calls in `gato-battle` match (441, 312, 624); all three pass 30000 fuzzed states |
| Field layer scrolling and battle status poses: `field_scroll_layers` `$C0:2C41` (`field/scroll.c`); `battle_select_status_pose` `$C1:2F97` (`battle/stats.c`) | | automatic BG1/BG2 scrolling in field bank code (4.4 fixed-point accumulators moving the scroll positions; of the movies, only `gato-battle` reaches it), and choosing a combatant's pose and animation from the first status bit set in its stat block through the table at `$CC:F77F`. All calls in every movie match (`gato-battle`: 2071, 105); both pass 30000 fuzzed states |
| Small battle helpers: `battle_setup_window_hdma` `$CD:0AEE`, `battle_call_c00002` `$CD:0C89`, `battle_pause_palette_animation` `$D1:F426`, `battle_frame_wait_tail` `$CD:04AA` (`battle/frame.c`); `battle_multiply_16x16` `$C1:C90B` (`battle/math.c`); `battle_number_to_digits` `$C1:011F` (`battle/gauges.c`); `battle_clear_ad9c_table` `$FD:ACEE` (`battle/buffers.c`); `battle_step_slot_effects` `$C1:3714` (`battle/timers.c`); `battle_points_are_close` `$CF:F957` (`battle/objects.c`) | | HDMA setup for the window, a register-preserving wrapper, pausing and restoring palette animation slots, the tail of the frame wait, a shift-and-add multiply, three-digit number tiles, a table clear, the eight effect slots' delay countdown with their handler table, and a proximity test. All calls in every movie match (`gato-battle`: 2054, 2070, 2054, 1725, 173, 312, 52, 344; `$CF:F957` isn't reached); all nine pass 20000 fuzzed states (the frame wait tail with `$CD02` clear, since its callee waits for interrupts while paused) |
| Input wrapper, play time, divide, button repeat, animation positions and object palettes: `input_update` `$C2:84D2`, `play_time_tick` `$C2:85AE` (`core/input.c`); `battle_divide` `$C1:C92A`, `battle_update_button_repeat` `$CD:0340` (`battle/math.c`); `battle_update_animation_positions` `$C1:4212` (`battle/objects.c`); `battle_step_object_palettes` `$CC:F1E7` (`battle/sprites.c`) | | the per-frame input work every mode runs (controller, button mapping and the play time clock, preserving all registers at the widths they had), the battle's 32-round shift-and-subtract divide, button auto-repeat, the six animation slots' positions through their handler table, and the four object palette animations. All calls in every movie match (`gato-battle`: 4008, 5460, 94, 1985, 1148, 1148); all six pass 30000 fuzzed states |
| Animated tiles, VRAM uploads, screen shake and gauges: `tile_animation_upload` `$FD:E022` (`core/tile_animation.c`); `battle_upload_alternating_vram` `$CD:0607`, `battle_vram_transfer` `$CD:3E8E`, `battle_update_screen_shake` `$CD:0B51` (`battle/frame.c`); `battle_draw_gauge` `$C1:06F0` (`battle/gauges.c`) | | the animated background tile uploads every game mode runs from its interrupt handlers (six of twelve slots per frame, alternating), the battle's per-frame VRAM clear or copy at `$5800`/`$5980` and its DMA transfer helper, the screen shake (random magnitudes and decaying amplitude through the multiplier, applied to the BG offsets `$CCC0` selects), and a party member's gauge in the window tilemap, computed with the hardware divider. `$CD:0607` ends with a `JMP` to `$CD:3E8E` on one path, so it stops there. All calls in every movie match (`gato-battle`: 1219, 1960, 84, 1985, 606); all five pass 30000 fuzzed states |
| Battle tile decoding and animation scripts: `battle_decode_tile_row` `$CD:1368` (`battle/tiles.c`); `battle_run_scripts` `$C1:4BBE`, `battle_run_script_channel` `$C1:4C9E` (`battle/scripts.c`) | | decoding a row of 2-plane tile graphics into 4-plane tiles for upload (the flip variants reached through `JSR ($13E6,X)` are ported inline, with the stack bytes their JSRs and PHYs write), and the battle's sixteen animation script channels, whose opcode handlers stay in the original as call-outs through `$C1:7A6B`. All calls in `gato-battle` match (48, 1148, 2031); all three pass 20000-30000 fuzzed states. Shadow mode now also reports where a routine was abandoned, which found a port passing its call site as 16 bits, so its call-outs waited for a return in bank 0 |
| Battle vblank uploads and frame update (`battle/frame.c`): `battle_upload_vram` `$CF:E300`, `battle_frame_update` `$CF:E58A` | | the two routines the battle main loop's frame wait runs each frame: the DMA uploads (OAM, palette, queued VRAM and object graphics transfers, each a write to `$420B` that the bus model runs to completion as the interpreter's write does), and the per-frame update: blink and frame counters, palette rotations, OAM priority bits, the demo exit and the L+R hold counter, then the effect, path, anchor, trail, prepared-sprite and object sprite routines, most of them native entries reached as call-outs. All 1985 calls of each in `gato-battle` match; both pass 10000 fuzzed states |
| Battle status flags (`battle/stats.c`): `battle_update_low_hp_flag` `$C1:B093`, `battle_update_status_flags` `$C1:B0B6`, `battle_update_all_combatants` `$C1:B223` | | per-combatant flag tables rebuilt from the stat blocks at `$5E00` (`$80` bytes per combatant), and the loop over all eleven that calls `$C1:B279` for each. All calls in `gato-battle` match (516, 1881, 216); all three pass 30000 fuzzed states |
| Battle object graphics (`battle/sprites.c`, `battle/math.c`): `battle_build_object_graphics` `$C1:1C55`, `battle_build_object_graphics_checked` `$C1:1C4A`, `battle_multiply_8x8` `$C1:0089`, `battle_multiply_8x16` `$C1:00A7` | | assembles an object's sprite tiles when its animation frame changes: rows of 32-byte tiles cleared, copied from its graphics set or copied through the flip table at `$C0:FD00`, then the frame's part offsets. Called a few hundred times over the fight but the largest remaining block of interpreted instructions. `$C1:1C4A` falls through into `$C1:1C55`; since that has its own entry, the port stops there (a native routine that continued into another entry's code would nest two shadow comparisons over the same instructions). All calls in `gato-battle` match (184, 181, 303, 450); all four pass 30000 fuzzed states, the graphics builder with the object's kind and graphics-set table (`$1C57`-`$1C7B`) kept, since a random data bank points its copies into I/O space |
| Battle per-frame work (`battle/frame.c`): `battle_frame_dispatch` `$CD:0C95`, `battle_frame_work` `$CD:088D`, `battle_flush_vram_transfer` `$CD:3EB4`, `battle_step_window_slide` `$CD:05B2`, `battle_update_scroll_dispatch` `$CD:06B4`, `battle_step_cdc9` `$CD:0ADE` | | what the battle NMI handler runs each frame: a dispatch on the battle mode, then the driver that sets the window registers and HDMA, steps the sliding window, and calls the frame's other work (mostly as call-outs, some of them ported routines). `$CD:06B4` ends in `JMP (abs,X)`, so the port stops at the jump target, where the scroll table port takes over. Indirect jumps and calls read their table from the program bank with 16-bit wrap (`m_jump_table`). All calls in `gato-battle` match (2054, 1985, 1985, 3970, 1985, 2054); all six pass 30000 fuzzed states except 3 of the dispatch's, where a random jump target is one of the dispatch's own instructions and the original reaches that address before its own jump |
| Battle IRQ handler (`battle/nmi.c`): `battle_irq` `$D1:FE97`, `battle_irq_after_hblank` `$D1:FEBF`, `battle_irq_update_ca22` `$D1:FE4C`, `battle_restore_math_operands` `$D1:FE71` | | the handler is split around its wait for hblank (an idle entry): the head saves the registers and stops at the wait with them pushed, the tail runs from the end of the wait to the `RTI`. The VRAM transfers it makes on odd frames (`$FD:E022`) stay in the original. `$D1:FE71`, which reloads the multiplier or divider operands, is shared with the NMI handler. All calls in `gato-battle` match (2069, 2069, 1042, 4123); all four pass 30000 fuzzed states. The head was the first entry whose stop point is below its entry on the stack, which shadow mode had taken for the original leaving the routine; it now checks against both |
| Battle sprite helpers (`battle/sprites.c`): `battle_step_object_paths` `$CF:EC78`, `battle_update_anchor_points` `$CF:ECC2`, `battle_record_object_trails` `$CF:ED18`, `battle_cycle_party_palettes` `$CF:E7D7` | | per-frame object movement along path tables, anchor positions for effects, a four-frame position history per object, and the party effect palette cycle. All calls in `gato-battle` match (1353 each for the first three, 1985); all four pass 30000 fuzzed states |
| Battle objects and slot loops: `battle_update_all_object_bounds` `$C1:283D`, `battle_update_object_bounds` `$C1:285A`, `battle_find_due_animation` `$C1:423A` (`battle/objects.c`); `battle_step_slot_delays` `$CC:F278`, `battle_step_effect_slots` `$CF:EFAF` (`battle/timers.c`) | | per-frame object work: screen bounds clamped at the edges, the head of the animation scheduler (it continues at `$C1:4269` when an object is due, so the rest of the scheduler stays in the original), and two loops that count down slot delays and call each expired slot's handler as a call-out (through `$CF:F01E`/`$CF:F02E` for the effect slots). All calls in `gato-battle` match (1493, 4489, 1148, 1148, 1985); all five pass 30000 fuzzed states |
| Controller input (`core/input.c`): `input_read_joypad` `$C2:84EC`, `input_map_buttons` `$C2:8545` | | reads the controller into the game's button order with new, held and auto-repeat bits, and maps them through the player's button assignments; used by the menus, field and battle. The wait for the automatic joypad read hands back to the original when it's still busy, and the L+R+Select+Start reset jumps to `$C0:FF00`. `$C2:84EC` samples `$4212` with `BIT`, which leaves V holding the hblank flag at that moment; V is dead after the return and listed as don't-care, since native and original sample at different times. All calls in every movie match (`gato-battle`: 19428 and 17771); both pass 30000 fuzzed states |
| Battle scroll tables and timers: `battle_update_scroll_tables` `$CD:06C4` and `battle_update_scroll_tables_bg3_moving` `$CD:076C` (`battle/display.c`); `battle_tick_timers` `$C1:8231` and `battle_dispatch_expired_timers` `$C1:8290` (`battle/timers.c`) | | the per-frame fill of the BG1-BG3 scroll HDMA tables, and two stretches of the battle main loop: counting down 143 timer entries, then finding each of 13 kinds' next expired entry and calling the kinds' handlers (as call-outs through the `$C1:B92D` table). `battle_tick_timers` is an entry in the middle of a routine: it runs from `$C1:8231` to the end of its loop and continues at `$C1:8290`, which shadow mode and the fuzzer compare like any early stop. All calls in `gato-battle` match (2054, 171 and 171; `$CD:076C` isn't reached); all four pass 30000 fuzzed states |
| Palette animation (`core/palette.c`): `palette_animate` `$FD:E39C`, `palette_animate_fade` `$FD:E82C`, `palette_fade_toward_white` `$FD:E8D7` | | the per-frame palette animation of every game mode: twelve slots at `$0520`, each with a type handler (the other six handlers and six fade sub-modes stay in the original as call-outs). The fade computes each color component with the hardware multiplier. All calls in every movie match (`gato-battle`: 16775, 133, 64); all three pass 30000 fuzzed states (the fades with A's high byte clear, as the dispatcher leaves it) |
| Battle work buffers (`battle/buffers.c`): `battle_clear_oam_buffer` `$CE:F066`, `battle_mark_object_grid` `$CC:F110`, `battle_reset_0b40_table` `$C1:1C3A`, `battle_clear_b328_table` `$FD:ACFD` | | per-frame resets that were among the most interpreted code left in the fight after the sprite pass: hiding the OAM buffer's sprites, copying the battlefield grid and marking the cells objects stand on (one `MVN` of 256 bytes), and two table fills. All calls in `gato-battle` match (1985, 1152, 420 and 189); all four pass 30000 fuzzed states. With these and the three new idle loops, `on` mode interprets about 28% fewer instructions over the fight |
| Battle wait loops (`battle/wait.c`): `battle_wait_for_frame_loop` `$C1:0080` and `battle_wait_for_nmi_loop` `$CD:3E81` (guarded: idle only while `$CD02` is clear), `battle_wait_vblank_end_loop` `$CD:3B84`, `battle_wait_hblank_loop` `$CD:3B8A`, `battle_wait_line_loop` `$CD:3B92`, `battle_wait_ca1f_loop` `$C1:003C`, `battle_irq_wait_hblank_loop` `$D1:FEB8` (in the IRQ handler) | | idle entries for the busy waits, which took about 40% of the instructions executed during the `gato-battle` fight (profiled over frames 19300-21200). In `on` mode the fight is still won, with timing differences in detail from an interpreter-only run. The scanline wait polls `$4212` for the end of hblank, which is why idle passes now stop at hblank boundaries rather than only at the end of the line |
| Field object motion (`field/motion.c`): `field_direction_to_target` `$C0:ABA2`, `field_set_velocity` `$C0:AC69`, `field_start_step` `$C0:ACFD`, `field_object_in_the_way` `$C0:5B95`, `field_update_walk_animation` `$C0:5614`, `field_stand_animation` `$C0:568B`, `field_line_up_with_object` `$C0:30B3`, `field_line_up_with_tile` `$C0:305D` | | the routines the movement commands called out to, so those commands now run entirely natively. All calls match in every movie (fair-companion: `stand_animation` 16079, `update_walk_animation` 4080, `start_step` 3898, `direction_to_target` 3891, `object_in_the_way` 3889); all pass 30000 fuzzed states (with `$6D` and `$E9` kept). `start_step` reads the map attributes of the tile ahead for sprite priority and picks a random direction when that tile is blocked; `object_in_the_way` reproduces a check whose `SBC` inherits the carry from an earlier `CPX` |
| `field_update_object_visibility` | `$C0:A88D` | 31378, all match; calls 7 unported routines; X, N, Z don't-care |
| `field_nmi` | `$C0:EA63` (NMI vector) | 7703, all match; also every call in `field`, `menu` and `fair`; calls about 25 unported routines |
| `decompress` | `$C3:0002` → `$C3:0557` | 54, all match |
| `field_wait_for_frame_loop`, `menu_wait_for_frame_loop`, `menu_wait_ready_loop` | `$C0:EC71`, `$C2:0348`, `$C2:81CA` | idle entries: the wait-for-next-frame loops, about 64% of all instructions executed |
| `attract_wait_loop`, `attract_vblank_end_loop`, `attract_vblank_start_loop` | `$7E:3023`, `$7E:31B0`, `$7E:31B7` | idle entries in RAM code, guarded by hash. The "hottest routine" by call count, `$7E:3176`, is an empty routine this loop calls |

The validator catches injected bugs: a wrong flag and a missing stack write
were each reported with the exact field. The first decompressor draft failed
all 54 calls on one detail, flags set by the final `PLD`/`PLB`, and the report
pointed straight at it.

With all five running natively over the new-game movie, 5108 of 7200 frames
are identical to the interpreter-only run at the same frame. Almost all of the
rest (2088) are the same frames one frame earlier, from approximating the
decompressor's time cost.

`on` mode's time accounting is approximate, so input can land on different
frames than in an interpreter-only run. The new-game movie still plays
identically. In the field movie, where walking is sensitive to that, 8658 of
11400 frames also appear in the interpreter-only run. Use shadow mode, not
`on` mode, to judge correctness.

A first attempt at the event script loop that runs each object's init script
(`$C0:58DE`) predated suspending call-outs. Back then a call-out interpreted
its callee on the spot with no time passing, so a handler waiting for a frame
spun forever. With call-outs running in the system loop, the same port matches
on every call. Shadow mode caught its one bug at the first call-out: `ASL A`'s
result wasn't stored back into A before calling a handler.

The object visibility port first failed 7108 of its calls on HDMA's line
counter and then 41 on HDMA-written PPU registers: state that moves with time
while the original runs. Both are now excluded from comparison for all
routines.

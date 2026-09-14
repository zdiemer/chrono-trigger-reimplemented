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
| `game/event/` | Event scripts: the engine loops and command handlers, grouped by kind (`flow.c`: jumps, conditionals, waits; `calls.c`: calling functions in other objects; `dialog.c`: dialog boxes; `animation.c`: object animation; `input.c`: input conditions; `movement.c`, `facing.c`, `properties.c`: objects; `variables.c`: object data into variables; `arithmetic.c`: variable arithmetic; `memory.c`: memory and pointers; `sound.c`, `screen.c`: sound and screen effects) |
| `game/field/` | Field mode: interrupt handlers, frame sync, objects |
| `game/menu/` | Menu mode: frame sync |
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
  exit. In `on` mode a waiting pass skips to the end of the scanline, so
  interrupts still land. `shadow` mode skips idle entries.
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
   conditions fall back to the interpreter.
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
as a JSR would (`--long` for JSL). The original runs on the interpreter, and the
native routine on a copy with call-outs interpreted in place. The machines are
compared where the native routine stopped: normally the return, or the
instruction where it handed back to the interpreter. States where the original,
or a callee, doesn't finish within `--step-limit` instructions are skipped:
random RAM sends some callees into endless loops.

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
| Event sound and screen: `play_sound` `$E8`, `play_song` `$EA`, `sound_81_83` `$EB`, `sound_command` `$EC`, `wait_apu_port1` `$ED`, `wait_apu_port3` `$EE` (`sound.c`; the sound driver at `$C7:0004` is called out); `brightness` `$F0`, `wait_brightness` `$F2`, `fixed_color` `$F1`, `wait_color` `$F3`, `set_bc` `$F4`, `set_1f` `$E3`, `copy_tiles` `$E4`, `queue_tile_copy` `$E5`, `set_layer_scroll` `$E6`, `scroll_screen` `$E7`, `party_routine_6_7`/`_6`/`_7` `$F8`-`$FA` (`screen.c`) | `$C0:3C05` … `$C0:3E75` | all calls match in every movie (fair-companion: `wait_apu_port3` 98, `play_song` 10); all 19 pass 5000 fuzzed states. The APU port waits read the ports directly and still match in shadow mode. Brightness and fixed color were identified from the field NMI port, which copies `$19` to `$2100` and `$21` to `$2132` |
| Event variables and memory: assignments, copies, arithmetic, bit operations, shifts and the random table (`$47`, `$4F`-`$52`, `$5B`-`$61`, `$63`, `$64`, `$67`, `$69`, `$6B`, `$6F`, `$71`-`$73`, `$75`-`$77`, `$7F`; `arithmetic.c`); reads and writes through script pointers, the `$7F:0000` memory area, memory bit operations and the MVN data copy (`$48`-`$4E`, `$53`-`$56`, `$58`-`$5A`, `$65`, `$66`; `memory.c`); latched input bits of `$51` (`$40`-`$44`; `input.c`) | `$C0:6740` … `$C0:6D09` | all calls match in every movie, though each is reached at most a few dozen times; all 45 pass 20000 fuzzed states (the data copy 3000). Pointer commands are fuzzed with `--range Y=0000-0000 --fill 7F2004-7F2004=7E-7F` so the pointer lands in WRAM |
| Event object properties and more movement: sprite flags `show_sprite` `$90`, `hide_sprite` `$91`, `set_sprite_flag_80` `$7E`, `show_object_sprite`/`hide_object_sprite` `$7C`/`$7D`; `set_1a00` `$89`/`_from_variable` `$8A`; positions `set_tile_position` `$8B`/`_from_variables` `$8C`, `set_position` `$8D`; `set_sprite_priority` `$8E` (`properties.c`); `move_in_direction` `$92`/`_keep_facing` `$9C`, `move_to_tile_from_variables` `$97`, `move_to_tile_counted` `$9A`, `move_to_tile_keep_facing` `$A0`/`_from_variables_keep_facing` `$A1`, `move_to_tile_with_velocity` `$7A` (uses the hardware multiplier and divider), `move_with_velocity` `$7B` (`movement.c`) | `$C0:4B49` … `$C0:5191` | all calls match in every movie (fair-companion: `set_1a00_from_variable` 124, `set_tile_position` 54, `move_to_tile_keep_facing` 53); all 20 pass 20000 fuzzed states. The move-to-tile commands share one port parameterized by each code copy's call sites |
| Event dialog, animation and waits: `set_dialog_table` `$B8`, `dialog` `$BB`, `dialog_top` `$C1`, `dialog_bottom` `$C2`, `choice` `$C0`, `choice_top` `$C3`, `choice_bottom` `$C4` (`dialog.c`); `loop_animation` `$AA`, `play_animation` `$AB`, `static_frame` `$AC`, `reset_animation` `$AE`, `loop_animation_0`/`_1` `$B3`/`$B4`, `play_animation_for` `$B7` (`animation.c`); `wait_4` `$B9`, `wait_8` `$BA`, `wait_16` `$BC`, `wait_32` `$BD`, `run_state_routine_once` `$AF` (`flow.c`) | `$C0:2E67` … `$C0:36DD` | all calls match in every movie (fair-companion: `dialog` 718, `play_animation_for` 265, `choice` 124); all 19 pass 20000 fuzzed states. Top and bottom box positions were confirmed from new-game movie screenshots |
| Event calls: `return` `$00`, `call` `$02`, `call_when_accepted` `$03`, `call_and_wait` `$04`, party member versions `$05`-`$07`, `refuse_calls` `$08`, `accept_calls` `$09`, `disable_object` `$0A`, `set_object_flag`/`clear_object_flag` `$0B`/`$0C`, `set_1c80` `$0D`, `set_arrival_flags` `$0E` (`calls.c`) | `$C0:5F74` … `$C0:62A4` | all calls match in every movie, though the movies reach them only a few dozen times. All pass 30000 fuzzed states, and the call and return commands another 30000 with the saved-position table (`$7F:0580-$7F:0DFF`) kept from the base state, so its empty slots exercise queueing and the return's level search |
| Event conditions, facing and variables: `if_mem0_below` `$18`, `if_object_value` `$1A`, `if_object_in_view` `$27`, `if_object_on_screen` `$28`; input conditions `$2D`, `$30`, `$31`, `$34`-`$39`, `$3B`, `$3C`, `$3F` (`input.c`); facing `$0F`, `$17`, `$1B`, `$1D`, `$1E`, `$1F`, `$25`, `$26`, `$A6`-`$A9` (`facing.c`); `set_object_value_from_var` `$19`, `_from_memory` `$1C`, `set_var_from_2980` `$20`, `get_object_tile` `$21`, `get_party_member_tile` `$22`, `get_object_facing` `$23`, `get_party_member_facing` `$24` (`variables.c`) | `$C0:5535` … `$C0:6732` | all calls match in every movie (`get_object_tile` 2978, `get_party_member_tile` 1868 in fair-companion); most aren't reached by the movies. All 35 pass 20000 fuzzed states. The input cells were identified by running the field movie and comparing with its buttons |
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

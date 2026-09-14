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
| `game/field/` | Field mode: interrupt handlers, frame sync, objects, event scripts |
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
6. Set `cost_milli` from the `original/native-bus` ratio `shadow` reports.

Liveness is only as good as the call graph. Functions called from code the
analysis can't see (RAM code, unresolved indirect jumps) keep everything live,
which is why step 1 feeds in the runtime call log. Rerun it when movies reach
new parts of the game, before relaxing a routine further.

## Status

| Routine | Address | Calls in new-game movie |
|---|---|---|
| `display_blank_and_stop_interrupts` | `$C0:0B4E` | 17, all match |
| `field_install_nmi` | `$C0:0B64` | 10, all match |
| `field_install_irq` | `$C0:0B75` | 10, all match |
| `field_run_object_init_scripts` | `$C0:58DE` → `$C0:9DC3` | runs at map load; all calls match in every movie (7-10 per movie). Event command handlers, some waiting for frames, run as call-outs |
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

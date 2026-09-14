# Porting routines

Ported code replaces original routines one at a time while the rest of the game
keeps running on the interpreter. Each port is validated call by call against
the original, independent of timing.

## Layout

| Path | What |
|---|---|
| `game/*.c` | Ported routines, grouped by subsystem. Every routine carries its original address in a `// $BB:AAAA` comment. |
| `game/natives.c` | Table registering each routine: address, name, entry precondition on P, and time cost in ON mode |
| `runtime/port/machine.h` | Helpers for touching the machine the way the original does: bus reads and writes, stack, returns, flags |
| `runtime/port/native.c` | Registry, interception, shadow validation, reporting |

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
- **`on`:** registered routines run natively in place of the original. A native
  routine finishes instantly, so the time the original would have taken is
  charged afterwards, one scanline at a time, so frame boundaries and line
  events still happen. Each call's cost is its own bus-access clocks times the
  routine's `cost_milli`/1000, so cost tracks data size.

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
   ported yet with `m_jsr`/`m_jsl`, giving the original call site, and make sure
   registers and flags are exactly what the callee expects at that point.
4. Register it in `game/natives.c` with its entry precondition (for example
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
| `c0_disable_display_and_interrupts` | `$C0:0B4E` | 17, all match |
| `c0_install_field_nmi` | `$C0:0B64` | 10, all match |
| `c0_install_field_irq` | `$C0:0B75` | 10, all match |
| `c0_update_object_visibility` | `$C0:A88D` | 31378, all match; calls 7 unported routines; X, N, Z don't-care |
| `c0_field_nmi` | `$C0:EA63` (NMI vector) | 7703, all match; also every call in `field`, `menu` and `fair`; calls about 25 unported routines |
| `c3_decompress` | `$C3:0002` → `$C3:0557` | 54, all match |
| `c0_wait_frame_loop`, `c2_wait_frame_loop`, `c2_wait_ready_loop` | `$C0:EC71`, `$C2:0348`, `$C2:81CA` | idle entries: the wait-for-next-frame loops, about 64% of all instructions executed |

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

The object visibility port first failed 7108 of its calls on HDMA's line
counter and then 41 on HDMA-written PPU registers: state that moves with time
while the original runs. Both are now excluded from comparison for all
routines.

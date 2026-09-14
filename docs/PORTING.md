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
  compared. Calls interrupted by NMI/IRQ are counted but not compared. Any
  mismatch prints the first differing field and makes `ct_dev` exit nonzero.
- **`on`:** registered routines run natively in place of the original. A native
  routine finishes instantly, so the time the original would have taken is
  charged afterwards, one scanline at a time, so frame boundaries and line
  events still happen. Each call's cost is its own bus-access clocks times the
  routine's `cost_milli`/1000, so cost tracks data size.

## Writing a port

1. Read the original with `python3 -m tools.disasm.linear BB:AAAA` and the
   listing. Check its callers in `disasm/out/functions.json` for the M/X widths
   it's entered with.
2. Write readable C that has the same observable effects. Keep all of them,
   including incidental ones: registers left behind, flags from the last
   instruction that set them, stack bytes written by push/pull, and I/O
   register writes in the same order. Use the data bank and direct page as the
   original does (`m_abs`, `m_dp`), not hard-coded banks.
3. Register it in `game/natives.c` with its entry precondition (for example
   8-bit A), so calls under other widths fall back to the interpreter.
4. Run `--native shadow` over the movie library. Every call must match. Then
   run `--native on` and compare frames with the interpreter-only run.
5. Set `cost_milli` from the `original/native-bus` ratio `shadow` reports.

As more of a subsystem is ported, incidental effects no caller depends on can
be dropped. Record that decision in the routine's comment, with the callers
checked, and give the comparison a mask for it rather than loosening it for
everyone.

## Status

| Routine | Address | Calls in new-game movie |
|---|---|---|
| `c0_disable_display_and_interrupts` | `$C0:0B4E` | 17, all match |
| `c0_install_field_nmi` | `$C0:0B64` | 10, all match |
| `c0_install_field_irq` | `$C0:0B75` | 10, all match |
| `c3_decompress` | `$C3:0002` → `$C3:0557` | 54, all match |

The validator catches injected bugs: a wrong flag and a missing stack write
were each reported with the exact field. The first decompressor draft failed
all 54 calls on one detail, flags set by the final `PLD`/`PLB`, and the report
pointed straight at it.

With all four running natively over the new-game movie, 5108 of 7200 frames
are identical to the interpreter-only run at the same frame. Almost all of the
rest (2088) are the same frames one frame earlier, from approximating the
decompressor's time cost.

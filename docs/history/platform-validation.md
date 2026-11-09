# Platform validation and initial timing design

Historical measurements and proposals moved from PLATFORM.md. Current behavior
is documented in [PLATFORM.md](../PLATFORM.md); old port counts and future-tense
statements below describe the revisions measured, not outstanding work.

### P3 renderer validation

**Results.**

`ct_dev --px-check --native on` over the whole movie library: **800751 frames,
every one identical to `render.c`'s at 256x224**, not a differing pixel. The
range-over and time-over flags agree on every frame, and no mid-frame change of
video memory ever ran out of snapshots. "Lines drawn" is how many of the frames'
non-blank lines the cache made the renderer redraw, which is the same number
`render.c` redraws.

| Movie | Frames | Identical | Spans per frame | Lines drawn |
|---|---|---|---|---|
| `title-start.ctm` | 1801 | all | 3.0 | 66.4% |
| `new-game.ctm` | 7201 | all | 12.8 | 32.9% |
| `menu.ctm` | 8902 | all | 16.1 | 27.6% |
| `field.ctm` | 11402 | all | 10.3 | 26.4% |
| `fair.ctm` | 12452 | all | 9.6 | 26.6% |
| `fair-companion.ctm` | 17903 | all | 8.9 | 23.7% |
| `gato-battle.ctm` | 21603 | all | 10.3 | 23.8% |
| `attract-demo.ctm` | 22500 | all | 25.9 | 55.0% |
| `saves/battle-tyrano-lair-walls.ctm` | 8043 | all | 19.4 | 32.0% |
| `saves/battle-golem-twins-gameover.ctm` | 12970 | all | 17.3 | 26.2% |
| `saves/battle-magus.ctm` | 18001 | all | 18.8 | 26.9% |
| `saves/events-blackbird.ctm` | 4401 | all | 16.6 | 50.2% |
| `saves/events-keepers-dome.ctm` | 5001 | all | 36.7 | 47.4% |
| `saves/events-end-of-time-gate.ctm` | 5601 | all | 119.5 | 48.5% |
| `saves/events-jetbike-race.ctm` (Mode 7) | 9401 | all | 50.7 | 46.8% |
| `saves/events-prison-escape.ctm` | 12901 | all | 7.3 | 23.0% |
| `saves/menus-end-of-time.ctm` | 8803 | all | 83.1 | 36.6% |
| `saves/menus-epoch.ctm` | 8805 | all | 68.5 | 51.0% |
| `saves/menus-shop-inn.ctm` | 9704 | all | 20.5 | 26.0% |
| `saves/menus-shop-equip.ctm` | 15513 | all | 43.6 | 24.9% |
| `saves/menus-field-menu.ctm` | 10602 | all | 55.2 | 25.3% |
| `saves/late-omen-terramutant.ctm` | 17763 | all | 32.1 | 30.1% |
| `saves/late-omen-battles.ctm` | 18413 | all | 21.7 | 26.0% |
| `saves/late-omen-zeal.ctm` (color math, windows) | 36923 | all | 35.5 | 44.4% |
| `saves/late-lavos.ctm` | 60250 | all | 29.1 | 25.2% |
| `saves/endings-beyond-time.ctm` | 105003 | all | 55.2 | 7.4% |
| `saves/endings-telepod-lavos.ctm` | 132888 | all | 33.4 | 9.9% |
| `saves/endings-bucket-lavos.ctm` | 196001 | all | 25.7 | 52.8% |

The per-line effects are what the span counts show: the battles run about 20
spans a frame, the menus and the Epoch 55-85, the End of Time's gate 120. That
is also what the decoder costs, since it rebuilds and compares a `VidLineState`
at every line whatever happens.

Speed, `ct_play` headless with `--native on --generated only`, on a shared machine,
runs alternating between the two renderers (five pairs for `gato-battle`, three
for the others):

| Movie | Frames | Scanline (median) | Native (median) |
|---|---|---|---|
| `gato-battle.ctm` | 21600 | 11.74 s (11.54-11.85) | 11.79 s (11.75-11.86) |
| `saves/events-jetbike-race.ctm` | 9400 | 5.61 s (5.46-5.64) | 5.55 s (5.39-5.79) |
| `saves/late-omen-zeal.ctm` | 36922 | 20.86 s (20.44-23.80) | 20.55 s (19.30-23.71) |

That is a wash, so P3's "measurably faster" isn't met: the two renderers do the
same drawing on the same lines, and the native one carries the decoder on top.
The 81% figure the milestone quoted was the renderer's share before it had a
cache; against the cached `render.c` there is nothing left to win by rendering
whole spans, and what remains to win is the decoder, which a build fed by service
calls wouldn't run at all.

The unit tests (including the platform lint and the new renderer lint) pass and
the build is warning-free with `-DCT_WERROR=ON`.

### The rewrites

The uses no backend can answer, and what each becomes (docs/TODO.md, "Timing
without a beam", tracks them):

| Routine | Now | Becomes |
|---|---|---|
| The event scheduler `$C0:5A46` | generated, beam budget | run due object scripts while `tm_frame_time_left(pf, o, limit)`; leave the rest queued for the next frame |
| The sprite frame queue `$C0:CA76` | generated, beam budget | the same over the animation queue, with its script-set limit |
| `$C3:02DD` for `$C3:01E4` | generated | the stir as game code (`scene_draw_random`, ported for the star field), over `tm_entropy` |
| The races `$C0:3B71` and `$C0:4181`; the waits before a mode enables its vblank handler, the field's `$C0:0B38` and the time warp's `$FD:EADA` (both reached through `tm_wait_frame_then_enable` now); the generated flag waits `$CD:044A`, `$CD:3E55`, `$CF:FCFB`; every idle entry on a flag a handler clears | idle entries, or generated | `tm_wait_frame` |
| The line handlers' hblank waits `$C0:ECEB`, `$D1:FEB8`, `$C3:0530`/`$C3:0538` | idle entries | gone: the blank is `vid_raster_display` at the handler's line |
| The controller-busy waits in `$C2:84EC` and `$C3:0A8E` | ports that stop at the original's loop | `in_ready` |
| `$CD:3B82` | idle entries reading the registers directly | `tm_wait_line`, or nothing once its purpose is known |
| The timer resets in `$C2:0043`, `$C2:940D` | generated (the boot's, `$FD:C000`, and the time warp's, `$FD:EB81`, are `tm_reset_timers`; the battle's line, `$D1:F1B0`, is `tm_set_line`) | a reset of timers nothing enables is `tm_reset_timers`, which a native backend leaves empty |

**Validation.** A call that re-expresses a use keeps its accesses, so shadow
mode compares it as it compares the ports now. The schedulers are the
exception: their loop runs a routine per object and reads the beam in between,
and a port that reads at a different moment from the original can decide the
budget differently. With the per-object work as a call-out, the clone is
reloaded from the original after each one and the port reads the beam a few
clocks before the original does; a read that falls across a line boundary is
the one disagreement left, and the first port measures how often it happens.
The native backend itself is checked the way target 4 says, by the routes and
gameplay: the routes must reach their outcomes with the budgets drained, the
handlers never preempting and the scene program's randomness from the host.

### The initial frame model

What follows from it:

- **Handlers never preempt the main thread**, so there are no lag frames.
  Work the original spread over two frames when the main thread overran is done
  in one, and the handlers' still-busy paths (the battle's interrupt-busy
  count, the scene's handler while its frame work runs) don't run.
- **Sections with the interrupts off need nothing.** A load with the handlers
  off (the menu code alone sets the interrupts at 29 places, most of them in
  pairs around a load) runs to completion inside one frame, and the black
  frames the original spent on it are gone.

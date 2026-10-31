# P1 as it was built: the CPU half's port and APU-direct

docs/PLATFORM.md's "P1 as built" until the sound driver's teardown
(docs/REIMPLEMENTATION.md, "The sound driver's teardown") deleted the port of
the CPU half in bank `$C7` (`game/core/sound_driver.c`), the audio service's
waits it made, the APU-direct backend (`runtime/platform/direct/`, `--audio
direct`) and `ct_dev --apu-port-margins`. Kept for what it measured and why
the pieces were shaped as they were; the paths it names are gone.

| Path | What |
|---|---|
| `runtime/platform/audio.h` | `au_driver_write`, `au_driver_read`, `au_driver_run_direct`, `au_driver_call` (the protocol level, below `au_command`; the call-out takes the callee's don't-care set since the $C7 pilot, history/reimplementation-waves.md), and the waits for the SPC700's answer `au_driver_exchange`, `au_driver_wait_echo`, `au_driver_wait_moved`, `au_driver_wait_echo_or_give_up` (below, "The CPU half's waits, as built"); `AuBackend`, `au_select_backend` |
| `runtime/platform/snes/audio.h` | the SNES backend, which hands the protocol-level calls to APU-direct while one of its runs is in progress |
| `runtime/platform/direct/` | APU-direct: synchronous runs of the driver, the APU's lead, the sample buffer |
| `runtime/apu/apu.c` | `Apu.lead`, `apu_run_ahead` |
| `runtime/port/native.c` | `native_hand_entry`, for running registered code synchronously |
| `game/core/sound_driver.c` | the rest of the driver's CPU half: the upload at `$C7:0000`, `$C7:09DA`, `$C7:09EA`, command `$70`'s and `$71`'s paths, retries, data crossing a bank, and every wait loop, all through the service |
| `game/battle/sound.c`, `game/menu/sound.c`, `game/scene/script.c`, `game/scene/sound.c` | the other code that touches the ports or the command buffer around them |
| `dev/io_log.c` | idle entries through services; the port mirrors `$2144-$217F` as their own group |
| `dev/ct_dev.c`, `frontend/ct_play.c` | `--audio snes|direct` |
| `dev/ct_fuzz.c` | `--apu-loopback`; idle entries can be fuzzed |
| `tests/test_platform_lint.py` | `core/sound_driver.c`, `scene/sound.c` and `scene/race.c` converted; `au_driver_*` allowed only in those three |

**Inventory.** `ct_dev --native on --io-log` over the 25 movies (the six main
movies, the 16 save movies and three save movies of the endings, `endings-*`,
which aren't in the repository yet), accesses to
the APU ports `$2140-$2143`:

| Origin | Before P1 | After P1 |
|---|---|---|
| hand ports, through `au_*` | 592 (`au_status`, events `$ED`, `$EE`) | 4401154 |
| idle entries, through `au_*` | | 25455776 |
| hand ports, direct (the sound driver) | 2717574 | 0 |
| idle entries, direct (the driver's four loops, the scene's port 3 wait) | 22531650 | 0 |
| generated blocks (the boot upload, `$C7:09DA`, command `$70`'s path, data crossing a bank, and what ran after the ports stopped there) | 4861081 | 0 |
| interpreter (the scene program's own send at `$7E:377E`, the race at `$7E:4BD4`) | 27729 | 0 |

Accesses to the mirrors `$2144-$217F` (2981364 before, 3353300 after, all from
generated blocks) are counted separately and were left alone, with this
evidence: every one is at `$217F` from the 16-bit `STA $217F` and `LDA $217F`
at 14 sites in `$CD:3810-$CD:3B05`, `$D1:FA4C-$D1:FB5D` and `$FD:DF0E-$FD:DF87`,
whose high byte is the access they're for: the work RAM port `$2180` (the only
instructions in the listing that address `$2144-$217F` at all). The low byte
lands on port 3, as on the hardware; it isn't a message to the driver (the
driver reads port 3 only right after the CPU side has written it as part of a
message), and both backends let it through. Porting them belongs to the video
subsystems that own the WRAM port (P2).

**What was routed.** Everything that touches the ports, reached by a movie or
not, now goes through the service:

- The driver (`core/sound_driver.c`): the upload `$C7:0000` (stops at its wait
  loops, which are idle entries, and entries `$C7:005F`, `$C7:007D`,
  `$C7:00B0`, `$C7:00FB` continue after them; the delay loop at `$C7:0118`
  and the rest of the routine have no port access and stayed in the original
  until the $C7 pilot made the countdown an idle entry, `$C7:011B`, and the
  rest `$C7:011E`),
  the counter send `$C7:09DA` and resend `$C7:09EA`, command `$70`'s echo
  (`$C7:08FF`), command `$71`'s upload (`$C7:07AE`, inlined in the command),
  the resends after a block header, the slot moves or the `$2F00` header gets
  the wrong answer (inlined as loops around a call-out to `$C7:09EA`), and data
  crossing into the next bank (`$C7:054C`, `$C7:06CD`, `$C7:07DF`, inlined).
  Twelve wait loops became idle entries (`$C7:0050`, `0078`, `00AB`, `00DB`,
  `00F6`, `060C`, `08CD`, `08D5`, `08FA`, `0912`, `091A`, `09F2`, with
  `0635`, `0A71`, `09E0`, `0A8B` from before). Every entry but `$C7:0000` and
  `$C7:0004`, which set it themselves, is guarded on a data bank that reaches
  the registers (since the $C7 pilot, on the driver's data bank `$00` and
  direct page `$1E00`, `sound_driver_frame_is_set`, as its work RAM is named
  in bank 0); no movie call is refused.
- The battle: the pause `$CD:3E49` (the "battle poll": `$CD:3E44` tests
  `$CD02` every frame and, while paused, sends `$F5` through `au_command`, and
  `$CD:3E65` sends it again when the pause ends; the test itself stays in the
  original, so the frequent call isn't an entry) and the loops waiting for port
  1 at `$CD:042B`, `$CD:0D9C`, `$CD:0DB7` (`au_status`).
- The menu: the waits around commands `$11` and `$14` at `$C2:CBC1`, `$C2:CC28`
  and the idle entry `$C2:CBE0`; the scene script command's port 2 wait
  `$C3:0C2A` (idle).
- The scene program in RAM (guarded by hash): its own send of the command in
  `$1E00-$1E03` (`$7E:377E`, `$7E:3780`, the echo wait `$7E:379B`), used for
  the pause sound and the race's sounds, and the race's port 1 and 0 writes
  (`$7E:4BD2`), through `au_driver_write`; the port 3 wait `$7E:5AAA` now reads
  `au_status`.

`au_driver_write` and `au_driver_read` are the protocol level: the driver's
CPU half and the scene's send are the SNES backend's implementation of
`au_command`, not game logic a native backend would keep, so the lint allows
them in those two files only. `au_status` stays the call for game code
polling the driver. `au_boot` wasn't added: no port calls the upload; the
registry entry at `$C7:0000` is the upload.

**The CPU half's waits, as built** (REIMPLEMENTATION.md, "Phase 4a, the
shipping mode's fallbacks, chunk 7"). The driver's CPU half waits for the
SPC700 through four more protocol-level calls, which wait where the port
stands rather than handing the machine back at the original's loop:
`au_driver_wait_echo(pf, PF_LOOP(head, exit), value)` until port 0 shows a
value, `au_driver_wait_moved` until it shows another, and
`au_driver_wait_echo_or_give_up(pf, PF_LOOP(head, exit), given_up, value,
tries)`, true where the echo came within the original's count of polls (its
loop leaves at `given_up` otherwise); and `au_driver_exchange(pf,
PF_CALL_DC(site, routine, set), value)`, the end of a message: the value on
port 0 and the wait for its echo, which the original makes by calling
`$C7:09DA` (or `$C7:09EA`, which takes the value from its cell). The SNES
backend waits with `native_wait_loop`, `native_wait_loop_or_give_up` and
`native_wait_call` on a wake for port 0 (`native_wake_port0_shows`,
`_leaves`; `port/native.h`): in the shipping mode the machine steps a pass of
the original's poll at a time (42 master clocks, 66 for a poll that counts
down) and, after an exchange, the time of the routine's return (102,
`native_wait_time`), since the SPC700 reads a message's last two bytes after
it echoes and a next message written sooner corrupted every third byte of a
song's sequence. The port's own time does the rest, and it has to put the
next message's bytes where the original's go: the loads of them through the
original's long pointer, which the port doesn't make, are charged before each
byte (`ACCESSES_A_LOAD`). `ct_dev --apu-port-margins` measures it: at every
echo, how long after the SPC700's read of port 2 and of port 3 the CPU's next
byte there comes. Over the movie library in the shipping mode, of 1,157,232
messages the next byte reached port 3 8-15 SPC700 cycles after the read
in 1,148,655 and 2-3 cycles after (about 63 master clocks, the least) in
182, as the original's do (1,499,256 and 193 of 1,511,199, the least the
same); port 2's least is 5 (the original's 3). Without the charge, port 3's
came 2-3 cycles after in 784,592 of them; in shadow mode and the tools the original's loop or routine
runs, the loop with A (and X) as the original sets them before it. APU-direct
runs the SPC700 ahead a pass at a time instead (`au_direct_wait_port0`).
A backend without an SPC700 never sees them.

**What changed from the plan, and why.**

- **The audio backend is chosen at run time**, not bound at compile time: the
  SNES backend's calls check whether an APU-direct run is in progress. Both
  backends need the same machine and the same ported driver, and validation
  must keep using the SNES backend, so a switch is simpler than two builds.
- **APU-direct runs the ported driver**, rather than the registry entry at
  `$C7:0004` delivering commands through code of its own: the CPU half of the
  protocol (block slots, relocations, the tables sent after a song) keeps its
  state in `$1E00-$1FFF`, which the game may read, so the backend reuses the
  code that is checked against the original and only changes what a wait
  costs. The boot upload and the scene's own send start runs the same way.
- **Protocol-level calls** (`au_driver_*`) were added below `au_command`, for
  the driver's and the scene's port accesses; the plan named only the
  command, status and boot calls.
- **The mirrors at `$217F` stay** (see Inventory).
- **Code no movie reaches was ported too** (the retries, command `$71`, the
  menu and scene script waits), validated only by fuzzing.

**APU-direct.** `ct_dev --audio direct`, `ct_play --audio direct`, with
`--native on` and the ported routines (the default is `--audio snes`):

- The registry entries that start an exchange, `$C7:0000`, `$C7:0004` and
  `$7E:377E`, call `au_driver_run_direct` first. With APU-direct selected it
  runs the code from there synchronously until PC leaves the driver (or the
  scene routine): registered entries directly (the same ports as with the SNES
  backend), idle entries a pass at a time, and instructions without an entry
  on the interpreter, with interrupts held off. JSRs the ports make to wait
  (`au_driver_call`) run the callee the same way instead of calling out.
- No main-CPU time passes: the run's bus clocks are cleared, so the job
  charges nothing. The time the code takes passes on the APU instead, before
  each port access and after each step: an idle pass takes its loop's
  `idle_clocks`, a native entry its bus clocks times its `cost_milli`, an
  interpreted instruction its exact time. So every wait runs the SPC700 until
  it answers, and the SPC700 sees the protocol with the spacing it has on the
  SNES backend (without it, answers read too soon after an echo went wrong and
  the attract sequence's song never started).
- The APU runs ahead of the machine by that time (`Apu.lead`, `apu_run_ahead`),
  and `apu_advance` absorbs the lead from the machine's following advances
  before running the APU again, so it keeps the machine's pace on average.
  Samples produced while running ahead are moved to a buffer outside the
  machine (up to 2^20 frames) so the S-DSP's own 4096 frames don't overflow;
  `au_take_samples` returns them first.
- A run that has had the SPC700 run 10 seconds ahead gives up and leaves the
  code in its wait loop, which then continues on the machine's timeline.
- Status polls (`au_status`) and the port mirrors read and write the APU
  directly, at the machine's time or ahead of it.

Upload and command time over the movies: 1.5-8.4 s of SPC700 time per movie
ran ahead of the machine (the boot upload is about 0.34 s of it); no run gave up.

**Where APU-direct changes behavior.**

- Uploads and commands take no frames: the boot, a new song (up to about 0.55 s
  of SPC700 time) and each command's echo wait. Everything after them runs
  earlier relative to the movie's input, which changes real-time battles and
  can change routes in long movies.
- The driver's timeouts (`$0400` passes) count on the SPC700's time, not the
  CPU's.
- The APU runs ahead after an upload and then pauses while the machine
  catches up; polls in that window see its later state, and in `ct_play` the
  sound arrives in a burst the audio buffer has to absorb.
- A song starts at nearly the same SPC700 time as with the SNES backend (the
  upload's SPC700 time matches), but game code the upload delayed on the SNES
  backend (the title screen's own driver commands after its song, for example)
  runs up to a few frames earlier, while input-driven sounds stay at their
  frames: sounds shift against the music by tens of milliseconds.

**Results.**

- I/O log: no access to `$2140-$2143` outside `au_*` in any of the 25 movies
  (29856930 accesses, all through `au_*`), generated blocks on.
- Lint and unit tests pass; the build has no warnings.
- Shadow (SNES backend): all 25 movies exit 0 with no mismatch, 2357230313
  generated-block checks included; the new entries' counts are in
  [PORTING.md](PORTING.md)'s status table.
- Fuzzing: all 50 routines of the driver and the other port code (idle entries
  included) and events `$ED`/`$EE` pass 20000 states each with `--apu-loopback`
  (constraints in PORTING.md).
- End states with APU-direct, by final frame, against the SNES backend
  (`--native on`) and the original timing (`--native on --generated only`,
  identical to `--native off`). `gato-battle` is won (the robot's "You won"
  dialog on the stage). 18 movies end in the original's room and scene:
  `new-game`, `menu`, `field`, `fair`, `fair-companion`, `gato-battle`, the
  five events, the four menus movies, `battle-tyrano-lair-walls`,
  `battle-magus` (in the Magus fight) and `battle-golem-twins-gameover` (back
  at the title). The SNES backend itself misses the original's end in seven
  long battle movies, where hand ports' estimated time already changes the
  fights ([REIMPLEMENTATION.md](REIMPLEMENTATION.md)); there APU-direct ends in the SNES backend's
  scene in four (`late-lavos` and `endings-bucket-lavos` still fighting
  Lavos, `endings-beyond-time` in the same field, `late-omen-terramutant` in
  the same room, after the fight rather than in it) and elsewhere in three:
  `late-omen-battles` (another Black Omen room; the SNES backend is in a third
  one), `late-omen-zeal` (fighting the Mammon Machine, where the SNES backend
  has lost to it and is back at the file screen and the original is fighting
  Zeal) and `endings-telepod-lavos` (on the 1000 A.D. world map, where the SNES
  backend is in the dark field it also ends in for `endings-beyond-time` and
  the original is fighting Lavos).
- Audio: `tools/audio_compare.py` with APU-direct against the SNES backend
  (`--native on`, same build; 4 s segments):

  | Movie | Audible segments | Waveform correlation (median, >= 0.9) | Spectrum correlation (median) | Log-spectral distance (median) |
  |---|---|---|---|---|
  | `new-game.ctm`, 7200 frames | 24 | 0.916, 16 of 24 | 0.9990 | 0.67 dB |
  | `field.ctm`, 11400 frames | 42 | 0.941, 31 of 42 | 0.9997 | 0.39 dB |

  `field` is as good as the Snes9x comparison on both measures (0.9997,
  0.39 dB), and `new-game`'s spectrum correlation too (0.999), but its
  log-spectral distance isn't (0.52 dB there), so that part of the criterion
  isn't met. The medians move with small timing changes: the same comparison
  before the new entries' `cost_milli` were set from shadow mode gave 0.9994
  and 0.59 dB for `new-game`, 0.9996 and 0.41 dB for `field`. Pure music
  segments are close (0.14-0.36 dB in `new-game`: the same song at a fixed lag,
  with the S-DSP's envelope counter at another phase, as in the Snes9x
  comparison); the distance comes from the title screen and name entry, where
  cursor sounds sit up to 5 ms differently against the music and the title's
  own commands after its song run five frames earlier (see above): 0.85-4.6 dB
  in those segments. The SNES backend against the original timing gives
  0.33 dB for `new-game`. Closing the gap would mean delaying the game by the
  time the SNES backend's uploads take, which is what APU-direct removes.


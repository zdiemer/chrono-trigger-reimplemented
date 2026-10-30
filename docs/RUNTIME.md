# Development runtime

`ct_dev` runs the original ROM on our own hardware model: the 65C816
interpreter, the SNES bus, PPU registers, and the APU with the SPC700 core. It
writes the same `.cttr` traces as `ct_oracle`, so `tools/trace.py diff` shows
where it departs from Snes9x. It's the scaffold that ported routines will plug
into (see [APPROACH.md](APPROACH.md)), and it never ships.

```sh
cmake -S . -B build/main -G Ninja && cmake --build build/main
build/main/ct_dev --rom roms/ct-usa.sfc --frames 1800 --trace traces/dev.cttr \
    [--movie oracle/movies/title-start.ctm] [--coverage traces/coverage.txt] [--status-every 60]
python3 tools/trace.py diff traces/oracle.cttr traces/dev.cttr --region WRAM
```

`--coverage` writes every executed instruction address with its M/X widths, for
seeding the disassembler. `--calls` writes executed call edges for liveness
analysis. `--save-state` writes the machine at the end of a run and
`--load-state` resumes from it (see Save states), which makes iterating on the
end of a long movie fast. Frame numbers and movie input continue from the
saved frame, and resumed runs are identical to straight runs. `--load-zst`
starts from a ZSNES save state instead, for coverage runs only (see ZSNES save
states below). `--wav out.wav`
writes the sound (32000 Hz stereo, see S-DSP below). `--io-log out.txt` counts
every hardware register access by what made it: a hand port and the platform
service call it went through, a generated block, the interpreter's instruction,
HDMA, or a DMA transfer ([PLATFORM.md](PLATFORM.md)). `--audio direct` (with
`--native on`) selects the APU-direct audio backend: the sound driver's uploads
and commands run the SPC700 ahead of the machine instead of taking CPU time
(`Apu.lead`, absorbed from the machine's following advances; PLATFORM.md, P1).
It changes timing, so it's never used for validation. `--spc-coverage out.txt`
writes every SPC700 address the sound driver executed, with counts, and
`--dsp-log out.txt` every S-DSP register write the driver made (frame, SPC700
cycle, register, value), through the APU's `on_insn` and `on_dsp_write` hooks;
both are for the sound driver's port ([SOUND.md](SOUND.md)).
`--sound-lockstep [out.txt]` runs that port (`game/sound/`) beside the
interpreted driver and compares them every tick (`--sound-lockstep-level`
status, keys, dsp or state; SOUND.md, "The port as built"); it needs a run
from power-on. A third hook, `Apu.host`, runs a program on the host in the
SPC700's place, and `Apu.host_boot` and `Apu.host_command` hand it the CPU
half's boot and commands as calls (`au_driver_boot_taken`,
`au_driver_command_taken`, given the command and the set of effects asked
for): `ct_play` plays the port that way by default, and `ct_dev` and
`ct_drive --sound-driver c` (`frontend/sound_host.c`). With `--mixer native`
the host makes the sound too: `Apu.host_mix` takes the cycles the S-DSP would
have run on, which then isn't run, and `Apu.host_take` hands
`apu_take_samples` the native mixer's frames (PLATFORM.md, "The native mixer,
as built").

## Components

| Path | What | Verified by |
|---|---|---|
| `runtime/cpu/cpu65816.c` | 65C816 interpreter | SingleStepTests, 5.12M cases, 0 failures |
| `runtime/apu/spc700.c` | SPC700 interpreter with cycle counts | SingleStepTests, 256K cases, 0 failures |
| `runtime/apu/apu.c` | Sound RAM, `$F0-$FF` registers, timers, boot upload protocol | Oracle traces |
| `runtime/apu/dsp.c` | S-DSP: BRR voices, pitch modulation, envelopes, noise, echo, 32 kHz output | `dsp_test`; Snes9x audio (see S-DSP) |
| `runtime/snes/bus.c` | HiROM map, `$4200` registers, joypads, DMA | Oracle traces |
| `runtime/snes/ppu.c` | VRAM/CGRAM/OAM access, decoded scroll/Mode 7/color registers, multiply, counters | Oracle traces |
| `runtime/snes/render.c` | Scanline renderer with Snes9x-compatible output colors | Snes9x framebuffers (see Renderer) |
| `runtime/snes/system.c` | Scanline timing, NMI/IRQ, HDMA, auto-joypad | Oracle traces |
| `runtime/snes/zst.c` | ZSNES save state import, for coverage runs (see ZSNES save states) | Chrono Trigger states run on |
| `runtime/snes/state.c` | Save states, written field by field so later builds load them (see Save states) | `state_test`; states resumed against uninterrupted traces |
| `runtime/port/generated.h` | Instruction semantics for generated code ([GENERATED.md](GENERATED.md)) | SingleStepTests, 5.12M cases with cycle counts; the interpreter on the SNES bus |

Build the conformance runners with the same CMake project (`sst_runner`,
`spc700_runner`); test data comes from `tests/cpu/fetch_sst.py` and
`tests/apu/fetch_sst_spc700.py`. `dsp_test` needs no data: it checks the
envelope rate counter, BRR decoding, and key-on, envelope, ENDX and echo
timing, and must print "all checks passed". `sst_generated_runner` (same CPU test
data) and `generated_bus_diff` check the C generator's instruction
translation ([GENERATED.md](GENERATED.md)); they're built when Python 3.11+ is found.

## Design notes

- **No boot ROM.** The console's SPC700 IPL ROM isn't included. `apu.c`
  implements its documented upload protocol directly: announce `$BBAA`,
  receive blocks at consecutive addresses, echo indices, then jump to the
  driver. It polls the ports the way the ROM does, because games write ports 0
  and 1 with one 16-bit write. Per-byte latency (`IPL_BYTE_CYCLES`) is
  calibrated so upload blocks land on the same frames as in Snes9x. When the
  driver jumps back to `$FFC0` with the ROM mapped (`$F1` bit 7), which Chrono
  Trigger has it do after a game over before uploading the driver again, the
  SPC700 returns to that protocol: direct page `$01-$EF` cleared, `$BBAA`
  announced. Reads of the ROM overlay itself still return RAM.
- **Frame boundary.** A frame ends at the start of vblank (scanline 225), after
  the NMI flag is set and before the handler runs. The next frame's first
  step enters the handler, or its second when /NMI arrived during the last
  cycle of the frame's final instruction (see Interrupt timing).
- **Work RAM port.** `$2181-$2183` set an address and each read or write of
  `$2180` accesses that WRAM byte and advances the address. Only the low 17
  bits select the byte, so `$00:3000` and `$7E:3000` are the same. Chrono
  Trigger's decompressor writes all of its output through this port, including
  code it then runs from `$7E:3000`.
- **Exact CPU timing.** Each instruction costs its bus accesses at their real
  speeds plus its internal cycles (6 master clocks each), both verified per
  opcode against SingleStepTests.
- **Time the CPU loses to other bus users.** As in bsnes and Snes9x, the CPU
  stops while something else has the bus:
  - work RAM refresh: 40 master clocks at hclock 538 of every line;
  - HDMA (as Snes9x counts it): on each line with an active channel, 18 clocks
    to stop and restart the CPU, 8 per byte transferred, and 8 per channel for
    its line counter (24 when it reloads an indirect address); the frame's
    setup at the start of line 0 costs 18 plus 8 (or 24) per enabled channel;
  - DMA: 18 clocks, then 8 per channel and 8 per byte, whatever the speed of
    the addresses read and written.

  A pause happens inside the step that crosses its position, and the rest of
  the step's time comes after it. Pauses don't move the point where the step
  samples interrupts: when one falls inside the last cycle, that cycle still
  began before it (`last_cycle_start` in `system.c`). The DMA cost is part of
  the `$420B` write, as before.
- **Interrupt timing.** The 65C816 samples its interrupt inputs at the start
  of each instruction's last cycle and takes a recognized interrupt after the
  instruction; an input that changes during the last cycle waits until after
  the next instruction (bsnes models it as an interrupt test one cycle early;
  Snes9x approximates it with a fixed delay after vblank starts). The system
  records, for every step, the master clock at which its last cycle began:
  6 clocks before the end if that cycle was internal, otherwise the length of
  the last bus access before the end (a DMA transfer counts as part of the
  `$420B` write that started it). An interrupt is taken after
  the first step whose last cycle begins at or after the interrupt's time.
  - NMI: RDNMI is set when line 225 starts, and /NMI reaches the CPU 6 master
    clocks later, as in bsnes (flag at H=2 clocks, /NMI edge 4 clocks after).
    Enabling NMIs through `$4200` while RDNMI is set counts from the end of
    the write, so it's taken one instruction later.
  - Timer IRQ: from the moment `$4211` bit 7 is set (no extra delay;
    hardware's is about 3.5 dots), with the I flag read when the IRQ is taken,
    so an IRQ pending across `CLI` is taken right after it rather than one
    instruction later as on hardware.
  - Interrupt entry sequences don't sample, so the handler's first instruction
    always runs; WAI polls on every idle cycle; a hand-written native routine
    counts as one step whose last cycle is its final 6 clocks. A generated
    block runs instructions back to back only up to the system's horizon
    (`snes_horizon`: the next line event or interrupt recognition), lets time
    pass up to its current instruction before I/O (`snes_sync`), and reports
    its last instruction's last cycle, so its interrupts and I/O land exactly
    where the interpreter's do ([GENERATED.md](GENERATED.md), "Exact timing").

  Chrono Trigger depends on this. Its location-change event command waits for
  vblank with `LDA $4210 / BPL` at `$C0:3B71` while NMIs are enabled, and the
  NMI handler (`$C0:EA63`, reached through `JML` stubs at `$00:0500`) reads
  `$4210` too. When NMIs were taken right after the instruction in which
  vblank began, the handler always cleared RDNMI before the loop read it, and
  the game hung on a black screen (Magic Cave cutscene, Lavos, Mammon Machine
  transition). With the sampling point, when /NMI arrives during `BPL`'s last
  cycle or before `LDA`'s read cycle, the NMI waits until after `LDA`, which
  sees the flag, and the loop exits within a few frames as it does on Snes9x
  and bsnes. On `new-game`, `menu` and `field` every framebuffer stayed the
  same as before the change; WRAM differs only in stack contents, one scratch
  byte and a few writes landing a frame apart. State version 3 carries the
  interrupt timing fields; version 4 the CPU pauses.
- **Oracle conventions.** Power-on RAM contents follow Snes9x so unwritten
  bytes compare equal: WRAM `$55`, SRAM `$60` (no save file), and a gradient
  preloaded into CGRAM. SRAM then matches Snes9x throughout the attract
  sequence, and CGRAM matches until the game loads its palette.

## Renderer

`runtime/snes/render.c` draws each scanline from the PPU register layer's
state when the system's `on_line` hook fires (start of hblank, before that
line's HDMA writes). Lines 1-224 become framebuffer rows 0-223 of a 256x224
XRGB8888 image. `ct_dev` hooks it up, writes its hash into traces, and saves
PNGs with `--shot-every N --shot-dir <dir>`. It's a low-level reference
renderer, kept separate from `ppu.c` so a native renderer can replace it.

That native renderer is `runtime/platform/native/render.c`
([PLATFORM.md](PLATFORM.md), P3): it draws a whole frame from the video
service's frame state rather than from the registers, is what `ct_play` shows by
default, and is pixel-identical to this one on every frame of every movie
(`ct_dev --px-check`). What follows still defines what both draw.

Supported:
- BG modes 0-7 with per-mode priorities, including the mode 1 BG3 priority
  bit; 2/4/8bpp tiles, flips, and 32/64-tile maps laid out as 32x32 quadrants
- offset-per-tile in modes 2, 4 and 6
- Mode 7 with flips, wrap, transparent or tile-0 fill, and EXTBG
- sprites with every OBSEL size, the 32-sprite and 34-tile limits with
  range/time-over flags, and priority rotation
- mosaic, windows with all four logic ops, main/sub screen window masks
- color math: add/subtract, half, sub screen or fixed color, clip to black,
  math windows, and direct color

Framebuffer colors follow the Snes9x libretro core so hashes can match
`ct_oracle` (`render_snes9x_color`, `render_rgb565_to_xrgb8888`). Snes9x scales
each palette color by master brightness first (`(c * brightness + 7) / 15`
per 5-bit channel), packs it as RGB565 with green's low bit copied from its top
bit, and does color math on those packed values. Full add saturates each
channel at the brightness-scaled maximum, half add averages the packed words,
and subtract and half subtract floor each channel at zero, with green
subtracted at 6 bits.

Checked against Snes9x by rendering the PPU state from its savestates and
comparing with its framebuffer for the same frame. The attract sequence (Mode 7
pendulum with sub screen math, mode 2 logo with offset-per-tile) matches
pixel for pixel, as does the new-game opening (mode 1, half-add sub screen).
Scenes that change PPU state mid-frame, such as split-screen dialog boxes and
HDMA menus, can't be checked that way. They need the development system to
run the game correctly first.

Gaps:
- Hi-res modes 5 and 6 and pseudo-hires are drawn 256 wide (every other hi-res
  pixel, main screen only); Snes9x outputs 512 wide, so those frames can't hash-match.
- Interlace and overscan (240-line) output aren't modeled.
- Which sprites lose tiles past the 34-tile limit follows OAM order from the
  first sprite; unverified against hardware.
- The ignored 16x16 tile-size bits of `$2105` have no effect, as on hardware.

`ct_dev` draws only when something reads the pixels: `--trace` (the hash),
`--shot-every` or `--px-check`. Otherwise each line runs `render_line_flags`,
which does the one thing drawing changes in the machine: the sprite range-over
and time-over flags `$213E` returns, from the same sprite evaluation.
`--native off` traces of `gato-battle` and `saves/battle-magus`, battles full of
sprites, are identical in every region whether lines are drawn, only flagged,
or not rendered at all, so nothing there acts on the flags; but a reader
elsewhere would see flags that are never set, so they are kept. Over the whole
suite, the shadow reports, coverage and call files of runs without drawing are
byte-identical to those of runs with it.

### Speed

The renderer's output is defined by the plain per-pixel algorithm: for each
pixel, walk the mode's front-to-back layer order, take the first layer whose
pixel is opaque, enabled on that screen and not masked by its window, then
apply color math. The code gets the same bytes faster in two ways.

Drawing a line:
- **A tile at a time.** Without mosaic, offset-per-tile or hi-res, a BG layer
  costs one tilemap lookup and one bitplane decode per 8 pixels: `tile_row`
  turns the 2, 4 or 8 plane bytes of a tile row into eight pixel values in one
  64-bit word through a byte-expansion table, a flip reverses the word, and a
  bit trick marks the opaque pixels. Fully transparent tile rows are skipped.
  The other cases take one sample per mosaic block.
- **Depths instead of order walks.** Every (layer, priority) pair appears at
  most once in a mode's order list, so each layer pixel gets its position in
  the list as a depth, and a screen keeps the largest: the same winner as the
  walk. Layers merge into the main and sub screens with branch-free loops that
  the compiler vectorizes. Window masks are built once per layer per line, and
  only when a window is enabled.
- **Color math chosen per line.** The sub screen's layers are only merged when
  color math with sub screen addition can apply, and the final loop is
  compiled separately for each add/subtract, half and sub screen setting.
- **Sprites per row.** Bitmaps of the sprites covering each row, rebuilt when
  OAM or the OBSEL size changes, replace the scan of all 128 sprites.
- **Palette.** CGRAM scaled by brightness is kept as RGB565 and updated per
  changed entry.

Skipping lines: over the movies, 60-87% of drawn lines come out identical to
the same line in the previous frame. Each line has a `RenderLineCache` entry:
a key holding every register and decoded value the line reads (with values
that can't matter zeroed: scroll of unused layers, Mode 7 values outside mode
7, window settings when no window is enabled, the fixed color when color math
is off), the sprite generation of its row, and the CGRAM entries and VRAM pages
(32 bytes, one 4bpp tile) its drawing read, with the generations current then.
When the key matches and none of those entries or pages has changed since, the
framebuffer row already holds the line, and the range/time-over flags drawing
set last time are set again.
- CGRAM and OAM are compared with the renderer's copies on every line (games
  change CGRAM mid-frame). A changed entry bumps its generation; a changed
  sprite bumps the rows it covered and covers.
- VRAM is only written outside active display, so it's compared with the
  renderer's copy (page by page when it differs) at line 0 of each frame, and
  within a frame only when `Ppu.vram_writes` changed, which only forced-blank
  writes do. The counter is a hint that emulation never reads; changes are
  found by comparing contents, so state loads and DMA need no hooks.
- Marking is conservative: a tile row marks its whole tile, a BG line its whole
  map row, and Mode 7, mosaic, offset-per-tile and hi-res lines depend on all
  of VRAM and CGRAM.
- Hosts must not write to `Renderer.pixels`: skipped lines aren't redrawn.

Wall time, `ct_dev --native on` on a shared machine, one run at a time; the
renderer share is timed inside `on_line`:

| Movie | Frames | Total before | Total after | Renderer before | Renderer after |
|---|---|---|---|---|---|
| `gato-battle.ctm` | 21600 | 39.5 s | 7.1 s | 33.6 s (84%) | 2.0 s (26%) |
| `saves/events-jetbike-race.ctm` (Mode 7) | 9400 | 13.3 s | 3.6 s | 10.3 s (79%) | 1.3 s (36%) |
| `saves/late-omen-zeal.ctm` (color math, windows) | 35300 | 61.5 s | 12.5 s | 52.6 s (83%) | 2.9 s (23%) |

Within the renderer, drawing BG tiles is about half the time, compositing
(merge and color math) a quarter, and the cache checks most of the rest. In
Mode 7 scenes most lines change every frame.

How identity was checked, and how to check a change to `render.c`:
- **Per line against the previous renderer.** A copy of `ct_dev` built with
  the previous `render.c` under renamed symbols runs it in `on_line` first on
  the same PPU state (restoring the sprite flags), then the new renderer, and
  compares the framebuffer row and both flags. The seven movies in
  `oracle/movies/` and all sixteen save movies, run to their full frame counts
  with `--native on` (315150 frames, 70.6 million lines), had no differing line
  or flag, and every frame's framebuffer hash equals the hash recorded with the
  previous renderer before the work started. Hash lists derive from the ROM, so
  they stay in the build tree.
- **State loads with a warm cache.** The same check with a state saved and
  loaded back 300-3700 frames later within one run (field, battle, Mode 7), and
  once, by accident, a different save's state: no differing line.
- **Emulation unchanged.** The six-movie `--native shadow` suite exits 0 with no
  mismatches, and `--native off` traces of `new-game.ctm` from before and after
  have no divergence, framebuffer hashes included. Against the Snes9x trace
  of the same movie, 3209 of 3233 non-blank frames in 1800-5399 are identical
  at the same frame, before and after.

## S-DSP

`runtime/apu/dsp.c` generates the sound. It lives inside the `Apu` and is
clocked by the SPC700's cycle count: `tick_timers` advances it with the same
cycles as the timers, and every 32 cycles (32000 Hz at the 1.024 MHz SPC700
rate) it produces one stereo sample. It never looks at wall time, so audio is
as deterministic as the rest of the machine. Samples collect in a 4096-frame
buffer inside the `Apu`; hosts take them with `apu_take_samples()` (ct_dev
`--wav` every frame, ct_play for SDL output, see [PLAYING.md](PLAYING.md)).
When nobody takes them, new samples are dropped and counted. Generating sound
changes nothing the CPUs see except what the DSP exposes: ENVX, OUTX and ENDX,
and the echo buffer it writes into sound RAM. The six movies run identically
in shadow mode, and a `--native off` run of `new-game.ctm` ends on the same
framebuffer as before the DSP existed.

The model is sample-granular (register writes land on the next sample, not on
one of the 32 clock phases within it) and follows the documented hardware
(fullsnes) and the behavior established by the accurate DSP emulators:

- **BRR**: 9-byte blocks decoded four samples (two bytes) at a time when the
  pitch counter passes bit 14, into a 12-sample history; shift 13-15 quirk,
  the four filters, saturation to 16 bits and then wrap to 15. End flag sets
  ENDX and jumps to the loop address; end without loop silences the voice
  (release, envelope 0) as soon as that block is current.
- **Pitch**: 14-bit pitch; PMON (voices 1-7) scales it by the previous voice's
  output; the counter is capped at `$7FFF`.
- **Interpolation**: the hardware's 512-entry Gaussian table, with the first
  three products wrapping at 16 bits and the last saturating.
- **Key on/off**: KON and KOFF are polled every other sample; a KON bit acts
  once. Key-on waits 5 samples (restart from the directory, three decodes to
  fill the history, no envelope, no pitch) before the envelope runs.
  FLG bit 7 releases every voice with envelope 0 each sample.
- **Envelopes**: attack, decay, sustain and release, direct GAIN and the four
  GAIN modes, stepped by one global counter (30720 values, counting down) with
  the 32-entry period table and its three phase groups. Phase changes don't wait
  for the counter; in GAIN mode the decay-to-sustain check reads the GAIN
  register, as on hardware.
- **Noise**: 15-bit LFSR stepped at the FLG rate; NON voices use it instead of
  BRR (BRR decoding, looping and ENDX continue).
- **Mixing and echo**: per-voice volumes with saturation after each voice;
  echo input from EON voices; the ring buffer at ESA (sampled a sample before
  use) with length from EDL (sampled when the offset wraps); the 8-tap FIR over
  15-bit history (first seven taps wrap, the eighth saturates); EFB feedback;
  FLG echo-write disable (left write uses the previous sample's FLG); MVOL/EVOL
  with their 16-bit product wrap; FLG mute.

Power-on values: FLG `$E0`, noise `$4000`, counter 0, other registers 0.

The DSP costs about 3% of run time (`gato-battle.ctm` with `--native on`:
30.3 s against 29.5 s). Its state is part of the machine's save states (see
Save states).

### Comparison with Snes9x

`tools/audio_compare.py` compares `ct_dev --wav` with `ct_oracle --wav` for
the same movie. Snes9x labels its DSP samples 32040 Hz (its SPC700 runs about
0.125% faster relative to the CPU), so the tool compares sample for sample and
finds each segment's lag by cross-correlation. Music the driver times itself
keeps a fixed lag; sounds the CPU starts land at different lags, which is what
lowers the numbers below where they overlap other sounds.

| Movie | Audible 4 s segments | Waveform correlation (median, >= 0.9) | Spectrum correlation (median) | Log-spectral distance (median) |
|---|---|---|---|---|
| `new-game.ctm`, 7200 frames | 24 | 0.933, 15 of 24 | 0.999 | 0.52 dB |
| `field.ctm`, 11400 frames | 42 | 0.940, 30 of 42 | 0.9997 | 0.39 dB |

Segments below 0.9 are the title menu, battle mode choice and name entry
(cursor and confirmation sounds on top of music) and one field event; in 0.5 s
windows those sections have lags jumping by a third of a frame between sounds,
and windows holding a single sound match at 50-63 dB SNR with over 70% of
samples identical.

Sample-exact equality holds wherever the timing agrees. The first half second
of title music is 99% identical. After that, envelope steps land up to a few
hundred samples apart because the rate counter runs from power-on and the song
starts at a different DSP time on the two systems; with the counter's start
value shifted by the lag (an experiment, not a setting), output matched Snes9x
exactly for the first 2.6 s of music, until the first note keyed on one KON
poll (2 samples) apart. Note starts differ by one poll in either direction,
with no bias, so it's SPC700-to-DSP timing rather than a latency error. Moving
the DSP's clock phase against the timers didn't remove it.

## Status

With no input, the development system runs boot, the sound driver upload, and
the attract sequence. Over the first 1800 frames, every non-blank frame it
renders is pixel-identical to a Snes9x frame.

Frames line up with an offset, not one to one, because of sound-driver
handshake races that emulators resolve differently:

- WRAM matches Snes9x exactly through frame 21.
- After the first driver command, we run one frame behind Snes9x and bsnes.
  Comparing only bytes the game has written, WRAM matches bsnes exactly for
  frames 23-26.
- The next transfer starts at frame 27 in bsnes, 28 here, and 41 in Snes9x. On
  that race Snes9x is the outlier, and the attract sequence ends up about 16
  frames behind ours.

With `oracle/movies/new-game.ctm` (title, battle mode, name entry, the opening
and the first dialog), the input resynchronizes both systems: after START,
frames match Snes9x with no offset. Over frames 1800-5399, 1665 of 3231
non-blank frames are pixel-identical to the same Snes9x frame and 3210 within
16 frames: the blank load before the opening starts one frame later than on
Snes9x (3209 against 3208, as before the CPU pauses were modeled) and now lasts
89 frames, as on Snes9x and bsnes, so the opening stays a frame behind until
4800 (before, a load one frame shorter than theirs made up for it: 3209 same
frame). In the dialog section (5400-7199), 1437 of 1548 match at the same frame
and 1544 within 16 frames (962 and 1342 before). WRAM there differs only in
frame-count and animation-timer bytes left over from the earlier timing
offset.

Longer movies build on it. All were timed on the development system and reach
the same scenes on Snes9x. The ones that play a whole ending were written by the
route scripts in `tools/movies/routes`, which decide what to press from the
game's memory ([MOVIES.md](MOVIES.md)); the rest were played by hand
([PLAYING.md](PLAYING.md)).

| Movie | Reaches | Frames |
|---|---|---|
| `field.ctm` | walks out of the starting room, down the stairs (map change), through a dialog and a second name entry, out the door onto the world map | 11400 |
| `menu.ctm` | from the starting room, opens the main menu, enters and leaves five submenus, closes it | 8900 |
| `fair.ctm` | `field.ctm`'s route, then across the world map into the fair and north through it | 12450 |
| `fair-companion.ctm` | `fair.ctm`'s route (without its last walk), then up to the plaza, the meeting event with the story character, picking up and returning the item they drop, and the character joining the party | 17900 |
| `gato-battle.ctm` | `fair-companion.ctm`, then out of the plaza's west edge to the singing robot's stage, the conversation that starts the fight, and the first battle, won (results at 21250 here, 21600 on Snes9x: battles run on real time, so they diverge in detail) | 21600 |
| `attract-demo.ctm` | no input: the intro, the title left idle, and the attract demo the game plays by itself (its field scenes, the world maps, its battle, the jet bike race and its other scenes), back to the title at 11290 and through it all again to the title at 22470; the second pass reaches a little the first doesn't, and running on reaches nothing more | 22500 |

Movies in `oracle/movies/saves/` start from power-on with a cartridge save
loaded (`--sram`, see [ORACLE.md](ORACLE.md)) to reach later parts of the game:
bosses, late-game techs, menus, shops, the Epoch, minigames and events. The
saves are the "SRM" set at fantasyanime.com/squaresoft/ctsaves-srm.htm (headers
`# Save: ctsaveNN-name`), and for the `endings-` movies the battery saves in the
emuWorks state archives at games.technoplaza.net/snes/chronotrigger/savestates.php
(headers `# Save: technoplaza ct-stateNN`; only the `.srm` is used). Each
movie's header names the archive and slot and carries the save's CRC-32, and
the frame count to run. They were timed on the development system. The saves
are committed in `oracle/saves/` (the fantasyanime, technoplaza and zophar sets;
see its README), and `ct_dev` finds the one a movie needs by its CRC-32:

```sh
build/main/ct_dev --rom roms/ct-usa.sfc --movie oracle/movies/saves/battle-magus.ctm --frames 18000 --native shadow
```

| Movie | Save, slot | Covers | Frames |
|---|---|---|---|
| `battle-magus.ctm` | ctsave07, 1 | Vamp ambush won (results), the Magus fight: Mid Tonic, Ice Water dual tech, Lightning2, barrier changes, a Revive, Cure | 18000 |
| `battle-golem-twins-gameover.ctm` | ctsave10, 3 | Delta Force, the Golems copying elements, Life revive, party wiped, game over back to the title (the sound driver restarts the SPC700 boot ROM) | 12969 |
| `battle-golem-twins.ctm` | ctsave10, 3 | Golem Twins won with alternating all-enemy elements, Dalton leaves, return to the save point and save slot 3 (route `golem_twins`) | 20216 |
| `events-ocean-palace-lavos.ctm` | recorded ocean-palace-after-twins, 3 | Mammon Machine, scripted Lavos loss, Crono's fall, Schala's escape, Zeal's destruction and waking in the Last Village, party exchange closed and field back in hand (route `ocean_palace_lavos`) | 30364 |
| `battle-lavos-gameover.ctm` | technoplaza ct-state16, 2 | New Game+ at the fair: the telepod's Gate to Lavos, the shell's fight lost with nobody acting, and the Day of Lavos that follows a loss to it: Lavos rising out of the 1999 A.D. world map, the operators' room, the 1000 A.D. world map laid back and turned in Mode 7 under the needles, and the globe behind "But... the future refused to change" (route `lavos_lost`) | 16192 |
| `battle-tyrano-lair-walls.ctm` | ctsave08, 1 | Volcano and Avian Rex: Volt Bite, Haste, won with an item drop and a level-up | 8042 |
| `battle-black-tyrano.ctm` | ctsave08, 1 | Tyrano Lair's throne room: Azala and the Black Tyrano beaten with Lapis and Mid Tonics, then Lavos falling to the planet (scene mode 4; route `black_tyrano`) | 49219 |
| `battle-guardian.ctm` | ctsave02, 3 | Arris Dome: the Guardian and its Bits, which it brings back, beaten (route `guardian`) | 25751 |
| `battle-r-series.ctm` | ctsave03, 2 | Factory Ruins: the lab security consoles, power and shutter escape, then the six R Series beaten by Crono and Lucca; Robo's repair and the Gate trip to the End of Time, with field control returned (route `r_series`) | 65437 |
| `battle-motherbrain.ctm` | ctsave14, 2 | Epoch to 2300 A.D.; Geno Dome's conveyors, charged pods, dolls and escort; Atropos and MotherBrain beaten, shutdown and field control regained (route `motherbrain`) | 135481 |
| `battle-unequipped-armor.ctm` | ctsave04, 2 | Marle's armor and helmet emptied with the equipment-menu glitch; Zenan Bridge, two Truce Canyon fights, the time gate, and Spekkio beaten with Ice (route `unequipped_armor`) | 22859 |
| `events-luccas-mother.ctm` | routes/luccas-mother-before-campfire, 1 | Fiona's campfire, the gate to 990 A.D., Lara saved with L, A, R, A, Robo's GreenDream and field control (route `luccas_mother`) | 15701 |
| `battle-rust-tyrano.ctm` | rainbow-shell, 1 | Giant's Claw's save point, the door switch and Rust Tyrano defeated; field back in hand (route `rust_tyrano`; normal cartridge save prepared by `rainbow_shell`) | 23282 |
| `events-cyrus.ctm` | ctsave14, 1 | Choras's tools and repairs, the Northern Ruins' left-wing guardians, Cyrus's ghost and the restored Masamune (route `cyrus`) | 39872 |
| `battle-ozzies-fort.ctm` | ctsave12, 2 | Epoch to 600 A.D.; Flea Plus, Super Slash, Great Ozzie and the trio, the switch battle and final cat trap; field control restored (route `ozzies_fort`) | 39237 |
| `battle-tech-sweep.ctm` | zophar chrono-trigger, 1 | Inside Lavos's shell: the second form and the Lavos Core fought with every single and double tech Crono, Magus and Marle have, each cast once (route `tech_sweep`) | 118090 |
| `late-omen-battles.ctm` | ctsave15, 1 | Black Omen random battles won (results), status Chaos keeping Lucca confused, many single techs, running away from an ambush | 18412 |
| `late-omen-terramutant.ctm` | ctsave15, 2 | TerraMutant (two-part boss): Delta Force, the dual tech list, Mega Bomb, Confuse, item menus, Life 2 on a fallen Crono | 17762 |
| `late-omen-zeal.ctm` | ctsave15, 3 | Queen Zeal won (Luminaire, Life 2, Cure 2, Hallation), then the Mammon Machine won (Antipode 3, Mega Bomb, Delta Force, Flare and more) and the start of the Zeal face-and-hands battle | 36922 |
| `late-lavos.ctm` | ctsave16, 1 | Lavos: the outer shell beaten with many techs, the DNA dialogue, then the core form (Crying Heavens, a MegaElixir, Cure 2, a revive) | 60249 |
| `endings-beyond-time.ctm` | technoplaza ct-state15, 1 | The final battle won (second form, then the Lavos Core), and the whole Beyond Time ending: the pardon, the Moonlight Parade, the farewells at the Gate, the bell and fireworks, and the staff credits to The End | 105000 |
| `endings-bucket-lavos.ctm` | technoplaza ct-state18, 2 | New Game+: the End of Time bucket, the Day of Lavos, the shell's boss imitations and the shell, the inner cavern, the inner Lavos and the Core, then the Good Night ending credits | 196000 |
| `endings-telepod-lavos.ctm` | technoplaza ct-state16, 2 | New Game+ with Crono and Marle: Taban's telepod at the fair, the outer shell, the inner cavern, the inner Lavos and the Core, then the whole Dream Project ending: the developer rooms, the Dream Team and the credits to The End | 132885 |
| `events-jetbike-race.ctm` | ctsave03, 1 | Lab 32: Johnny's challenge and the Mode 7 race with turbo boosts | 9400 |
| `events-jetbike-race-won.ctm` | ctsave03, 1 | Lab 32: the race won, recorded by hand: the boosts late against Johnny's catch-up, the finish and the scene after it | 8083 |
| `events-end-of-time-gate.ctm` | ctsave03, 3 | End of Time pillar, yes/no prompt, gate effect, arrival in Medina | 5600 |
| `events-prison-escape.ctm` | ctsave01, 3 | Prison countdown captions, the march to the guillotine, Lucca's rescue | 12900 |
| `events-keepers-dome.ctm` | ctsave09, 1 | Keeper's Dome: the pendant opens the sealed door, Belthasar's hall | 5000 |
| `events-fionas-forest.ctm` | ctsave12, 3 | The sapling saved in 12,000 B.C., Sunken Desert and Retinite won, Robo left with Fiona in 600 A.D., recovered 400 years later, and the campfire through Lucca back in hand (route `fionas_forest`) | 36440 |
| `events-blackbird.ctm` | ctsave11, 1 | Blackbird escape onto the hull over scrolling clouds | 4400 |
| `events-death-peak-wind.ctm` | ctsave11, 3 | Death Peak's guide becomes a tree; shelter through the gusts at both trees and reach the next field (route `death_peak_wind`) | 4676 |
| `events-death-peak-ascent.ctm` | ctsave12, 1 | First two Lavos Spawn beaten, middle cave opened, slippery ridge crossed, cartridge save at the last save point (route `death_peak`) | 23852 |
| `events-death-peak.ctm` | routes/death-peak-summit, 1 | Final Lavos Spawn beaten, shell pushed and climbed, summit Time Egg, clone exchanged for Crono, End of Time field back in hand (route `death_peak_return`) | 23856 |
| `events-epoch-lavos.ctm` | ctsave14, 1 | The winged Epoch dialed to 1999 A.D., Lucca's question over Lavos, Fight!, and the dive through its shell (scene mode 6; route `epoch_lavos`) | 7792 |
| `menus-field-menu.ctm` | ctsave14, 3 | Every field submenu: equipment, items (use, organize), techs, config, party order, saving | 10600 |
| `menus-shop-inn.ctm` | ctsave14, 1 | Truce Market buy and sell, resting at the inn | 9702 |
| `menus-shop-equip.ctm` | ctsave14, 1 | Medina market: down from the shop's tabs to the party, members' equipment changed from there (a member's slots, the list of what fits), weapons and armour bought | 15513 |
| `menus-epoch.ctm` | ctsave14, 1 | Epoch: Mode 7 flight, map view, time travel between eras, landing, buildings | 8800 |
| `menus-end-of-time.ctm` | ctsave12, 2 | Party exchange, the Guru of Time, saving over a slot | 8800 |
| `menus-save-point.ctm` | ctsave12, 2 | The End of Time's save point opening its own save screen: no to saving over the first slot, then a save over the third (route `save_point`) | 3270 |
| `menus-sweep.ctm` | ctsave14, 1 | The field menu on the 1000 A.D. map: item tabs and users, the techs screen slid past three members and eight techs, the equipment members slid to a reserve member, Settings' window design and custom buttons (route `menu_sweep`) | 6424 |
| `menus-new-game-plus.ctm` | technoplaza ct-state16 | The load screen of a finished cartridge: the erase question on the second controller answered no, then yes; New Game+ from the fourth row, carrying on from the first slot, the battle mode and naming, onto the 1000 A.D. world map (route `new_game_plus`) | 3116 |
| `field-65m-bc.ctm` | ctsave05, 1 | The 65,000,000 B.C. world map on foot with followers, into a location and back out (route `field_65m_bc`) | 2921 |
| `field-65m-bc-dactyl.ctm` | ctsave07, 3 | The 65,000,000 B.C. world map on a dactyl at Tyrano Lair: boarded, flown a short way, landed and got off (route `dactyl_65m_bc`) | 2810 |
| `field-12kbc-1f4.ctm` | ctsave08, 2 | The 12,000 B.C. world map at `$1F4`, into a location and back out (route `field_12kbc_1f4`) | 2953 |
| `field-12kbc-1f5.ctm` | zophar chrono-5, 1 | The 12,000 B.C. world map at `$1F5`, likewise (route `field_12kbc_1f5`) | 2993 |
| `field-12kbc-1f6.ctm` | zophar chrono-6, 2 | The 12,000 B.C. world map at `$1F6`, likewise (route `field_12kbc_1f6`) | 2873 |
| `field-12kbc-after-the-fall.ctm` | ctsave11, 2 | Out of the Earthbound Village onto the frozen 12,000 B.C. map after Zeal's fall, the winged Epoch parked beside it, into the next location and back out (route `last_village_12kbc`) | 9269 |
| `events-magic-cave.ctm` | ctsave06, 1 | The Magic Cave's mountainside: the party walks up to the rock and Frog, holding the Masamune, remembers: the forest, then Glenn and Cyrus on the bridge, a few lines in (route `magic_cave`) | 6156 |
| `battle-denadoro.ctm` | ctsave04, 2 | Into the Denadoro Mountains from the world map, up to the monsters until they attack, the fight won (route `denadoro_fight`) | 9532 |

All run in shadow mode with no mismatches. Making them found two hangs in the
development system, both fixed: the location-change vblank wait (see Interrupt
timing) and the SPC700's return to its boot ROM after a game over.

They were first timed before the CPU's pauses for work RAM refresh and HDMA were
modeled and DMA's cost corrected (see the battle glitches below). With those,
battles and other real-time sections play out differently: on Snes9x the old
inputs didn't reach the listed scenes either (`battle-golem-twins-gameover`
showed the same frames on Snes9x and here at 6000, 9000, 11000 and 12000 only
after the fix). The battle movies, `menus-shop-inn` and the late-game movies
and the `endings-` movies were re-authored on the corrected timing and reach
their listed scenes again. The ending movies were played by a driver that reads
game memory (whose turn it is, cursors, HP) and backtracks through save states
when a fight is lost, so their routes can be replayed on a future build.

Non-blank frames pixel-identical to Snes9x at the same frame / within 16 frames:

| Section | `menu` | `field` | `fair` |
|---|---|---|---|
| 7200-8899, menu | 1555 / 1594 of 1639 | | |
| 7200-8999, walking the house | | 1184 / 1448 of 1752 | same as `field` |
| 9000-10799, dialog and name entry | | 48 / 183 of 1690 | same as `field` |
| 10800-11399, world map | | 40 / 540 of 600 | |
| 10800-12449, world map and fair | | | 153 / 1322 of 1556 |

Before the CPU's pauses were modeled the numbers were 1264 / 1453 (menu),
166 / 482 (house), 48 / 185, 216 / 600 and 381 / 984. The house frames that
matched no Snes9x frame then looked the same but differed in a few hundred
pixels around the characters (87 pixels in rows 97-112 at frame 8700), with a
coordinate byte off by one (`$7E:01AB`): direction holds landed one frame apart
relative to the game's frame counter, so positions drifted by a pixel. That
was mostly the CPU running about 3% fast without work RAM refresh, not input
phase: with refresh alone added to the DMA fix, 1012 of them matched. The world
map now comes up three frames after Snes9x's: its load starts a frame later,
as before, and lasts 48 frames, against 47 on Snes9x and bsnes (46 before).

`fair-companion.ctm`, same metric:

| Section | Result |
|---|---|
| 12200-13899, fair and plaza (walking, waiting) | 448 / 1129 of 1655 |
| 13900-15899, meeting event, item pickup and return | 712 / 1159 of 2000 |
| 15900-17899, dialog and joining | 111 / 654 of 1940 |

Before the CPU's pauses were modeled: 137 / 146, 426 / 1302 and 835 / 1696.
Both systems run the same events (screenshots every 200 frames show the same
scenes); in the last section most frames now differ from every nearby Snes9x
frame in a few hundred pixels of sprites (654 pixels at 16700), an animation
phase that happened to agree before. Two spots were checked by
screenshot on both systems and looked identical: a vertical seam in the fair's
east side (part of the map) and a spot at the north stairs where the player
is blocked.

The battle movie found a DMA bug: the address step direction was taken from
bit 1 of `$43x0` (part of the transfer mode) instead of bit 4, so mode 2
transfers ran backwards. The battle transition copies the palette from
`$7E:2200` with mode 2, and got zeros instead of the field colors, showing 70
frames of garbled graphics where Snes9x keeps the field image. Fixed; the
transition now matches Snes9x frame for frame.

Fuzzing found another: a DMA transfer whose destination covers `$420B` wrote
to it, which started a transfer inside the transfer, and so on until the host
stack overflowed. The game never does this; random register values did.
Transfers no longer nest.

Playing the first battle showed single frames with the top of the picture black
or the battle window missing (the field showing down to row 208 and a black
band at the top), which Snes9x never shows. The battle's NMI handler
(`$D1:E77C`) forces blank, turns HDMA off and uploads VRAM, OAM and CGRAM by
DMA; the per-frame work it calls turns HDMA back on (`$CD:08FE`) and restores
the brightness (`$CD:0925`). On busy frames (seven transfers, 3872 bytes) that
finished on line 8 of the next frame: forced blank until then, and HDMA was
off at the frame's HDMA setup on line 0, so none of the window's HDMA ran that
frame. The IRQ at line 211 forced blank below the window as usual. The cause
was CPU time: DMA was charged the speed of each byte's read and write (12 or
14 clocks) instead of 8, which made those vblanks about 16 lines too long. Fixing
that alone made the CPU faster than on hardware, because two other costs were
missing: work RAM refresh (3% of every line) and HDMA's CPU time. With all
three, the latest restore in that battle is 7 lines before the frame starts.
Repro: `gato-battle.ctm` with `--native off` had 15 of these frames between
19308 and 20921 before, none after; so did a movie of the same route recorded
on `ct_play` (frames 13898, 13982, 14305), which after the change still
reaches and plays the battle, with no full-width one-frame change in 13300-14430.
Scan with screenshots every frame for frames that differ from both neighbours;
the blinking target cursor is the only one left. With `--native on` (what
`ct_play` runs) `gato-battle.ctm` still shows 11 such frames (95 one-frame
changes before, 17 now, cursor included): hand ports charge estimated time
and interrupts waited until it was paid (PORTING.md), so an NMI could arrive
dozens of lines late (frame 19313: no NMI in the frame before, then one at line
27). That was the ports' time estimate, not the hardware model; the timing gate
takes interrupts during a port's time now (REIMPLEMENTATION.md, "The timing gate"). Load lengths now agree better
with bsnes (accuracy core) as well: in `new-game`, the static runs at 3208,
5591 and 6134 last 89, 252 and 36 frames in bsnes and here, 88, 250 and 30
before; the world map load in `field` lasts 47 frames in bsnes, 48 here (46
before).

## Validation strategy

External emulators check that the hardware model is right. They aren't
required to match frame for frame, because sub-frame races with the sound
driver resolve differently even between Snes9x and bsnes. Ported routines are
checked against this development system running the original code, which is
deterministic and must match exactly.

To compare against bsnes, whose power-on RAM is randomized, compare only the
WRAM bytes the game has written: `ct_dev` traces carry a `WRAMW` region, and
`tools/trace.py diff --written-only [--offset N]` uses it.

## Next

What the development runtime itself still owes is in [TODO.md](TODO.md),
"Correctness and accuracy": the IRQ timing simplifications, the S-DSP's KON
poll, and re-measuring the audio comparison table, which predates the DMA and
refresh timing fix. The game-side work the runtime exists for is
[PLAN.md](PLAN.md); the regression check for it is the whole movie library in
shadow mode (`python3 tools/suite.py`, docs/PORTING.md).

## Save states

`ct_play`'s quick saves, `--save-state`/`--load-state` in `ct_play`, `ct_dev`
and `ct_drive`, and `ct_fuzz`'s base states are one format
(`runtime/snes/state.c`): the machine written field by field under names, so a
state saved by one build loads in a later one whose structures have changed.
They used to be the raw `Snes` image, which stopped loading whenever a
structure changed (P3 showed it by changing `sizeof(Snes)`).

| Bytes | Holds |
|---|---|
| 4 | `CTST` |
| 2, 2 | format version (2), flags (0) |
| per field | name length (1-255), the name (`cpu.a`, `ppu.vram`, `apu.dsp.voice[3].env`), element size (1, 2, 4 or 8), element count (4 bytes), the elements |
| 1 | 0: end of the fields |
| 4 | CRC-32 of everything before it |

Every number is little-endian whatever the host, so states also move between
hosts. The first field is `rom.crc32`, the ROM the state was saved with; the
rest are every field of `Snes` except the ROM, host pointers, hooks,
`halted` and the write tracking shadow mode uses (below), about 300 of them in
290 KB (work RAM, VRAM and sound RAM are most of it).

A program on the host that plays in the SPC700's place keeps fields of its
own, which a state holds beside the machine's: `snes_state_write_with` and
`snes_state_read_with` take an `SnesStateExtra` that names every one of them
(the C sound driver's and its host's, under `sound.`, from
`sound_host_state`), and a read fills a copy first and hands it over only once
the whole state has checked out. Such a state also has an `apu.host` field,
the program's number (`SNES_STATE_HOST_*` in `snes/system.h`; 1 is the C sound
driver through the S-DSP, 2 the C sound driver through the native mixer, whose
fields are under `sound.mixer.`), and a state loads only the
way it was saved: with that program's fields, or without any, since the
SPC700's state means nothing to a program, nor one program's fields to another
or to the SPC700 (format 2; a format 1 state loads without them).
`snes_state_save_with` and `snes_state_load_with` do the same to a file, and
`sound_host_copy` and `sound_host_restore` are the host's half of an in-memory
copy (`ct_drive`'s slots).

Nor is the native runtime (`runtime/port/native.c`). With `--native on` a
hand port may be suspended in a call-out at a frame's end (`native_suspended`),
or owe estimated time the machine hasn't let pass yet (`native_owed`); a load
restarts the runtime, so a state saved then would leave the routine to the
original code and drop the time. The tools save where both are 0: `ct_drive`
replays from such a frame (MOVIES.md, "Native code"), `ct_play` waits for one
(PLAYING.md, "Saves"), and `ct_dev --save-state` warns.

Write tracking isn't state. Shadow mode refreshes its machine copies with only
the pages of work RAM and VRAM written since the last refresh
([PORTING.md](PORTING.md), "What shadow mode costs"), from a write epoch the
bus and the PPU stamp on each page. A load replaces those memories without a
single write, so it keeps the machine's own epoch counters and stamps every
page with the current one (`snes_mark_all_written`): the next refresh of any
copy takes all of both, whether or not the native runtime is restarted. The
ZSNES import and `ct_drive`'s `poke` and slot `load` do the same.

A load looks up each field this build has by name, into a machine powered on
with the same ROM, and copies that machine in only once everything checked out:

- a field the state lacks keeps its power-on value: the state is from a build
  before the field existed;
- a field the state has and this build doesn't is skipped: the state is from
  a later build, or the field was removed;
- the state is refused, with a reason and the machine left as it was, when its
  checksum fails (truncated or damaged), its magic is wrong (the old raw
  images, `CTSS`, are named as such), its format version is newer, it was
  saved with another ROM, a field's element size or count differs from this
  build's, a bool isn't 0 or 1, or a value the emulation indexes or loops with
  is out of range (`snes.stall_next`, `snes.vline`, `snes.hclock`,
  `ppu.oam_addr`, `ppu.oam_reload`, `apu.phase`, `apu.dsp.out_count`,
  `apu.dsp.fir_pos`, a voice's `env_mode`).

So adding a field to `Snes` or anything in it means adding its line to
`visit()` in `state.c`, and a field whose meaning changes gets a new name
rather than being read as the old one. Enums are written as a byte, since their
size is the compiler's.

`state_test` (no ROM; CI runs it) checks this on a small synthetic program:
states round-trip every field and byte, and the loaded machine runs on
identically; a state without a field (an older build's) loads with it at its
power-on value, and one with an unknown field (a newer build's) loads; every
prefix of a state, flipped bytes, the raw magic, a newer version, a resized
field or array, a duplicate field, out-of-range values and another ROM's state
are all refused with the machine unchanged; a host program's fields round-trip,
reach it only once the state has loaded, and load only the way they were
saved. It also checks that every byte of
`Snes` is a state field, a host pointer or hook, write tracking, or padding, so
a field added without its line in `visit()` fails the test (a forgotten field
small enough to pass for padding is caught by the byte-for-byte round trip
instead, if the program changes it). And that write tracking survives a load
correctly: a copy refreshed before a load, refreshed again after it, equals the
loaded machine in work RAM and VRAM, and the state holds no tracking; with the
load's `snes_mark_all_written` removed, that check fails.

On the game: a state saved at frame 2500 of `battle-magus.ctm` (with
`--native on --generated only`) and resumed traces identically to the
uninterrupted run over frames 2500-3999. Across builds: with a field added to
`Ppu` (so `sizeof(Snes)` changed), the build with it loaded the state the build
without it had saved, and the reverse, and both resumed to traces identical to
the uninterrupted run; a state written by a build from before this format is
refused as a raw image.

A state still can't hold a native routine suspended in a call-out
([PORTING.md](PORTING.md)), which is why `ct_play` waits for none to be before
a quick save.

## ZSNES save states

`ct_dev --load-zst <file> [--frame F]` (and `ct_play --load-zst <file>`)
starts from a ZSNES 1.51 save state (`.zst`, `.zs1`-`.zs9`, header
`ZSNES Save State File V143`) instead of power-on. It exists to reach code no
movie reaches yet, for coverage files and miss reports, and to put a
movie-authoring session next to a boss or an ending quickly. **A run from an
imported state is never a validation run:** it can't be replayed from
power-on, and the import approximates state the file doesn't hold in our form
(below), so what follows isn't exactly what the original would do. Movies must
still start from power-on (with `--sram`, which `--load-zst` replaces).

```sh
build/main/ct_dev --rom roms/ct-usa.sfc --load-zst "roms/saves-technoplaza/ct-state18/Chrono Trigger (U).zs4" \
    --frames 30000 --movie a-presses.ctm --coverage traces/cov-zst-state18.txt --calls traces/calls-zst-state18.txt
```

Frames are numbered from `--frame` (default 0), so a movie's input applies
relative to the import; `--frames` is still the frame to stop before. ct_play
doesn't write the state's cartridge save back to `<rom name>.srm`, and can't
record from an import.

### Format

The layout, from ZsKnight's `zst_frmt.txt` and _Demo_'s SPC section
description (romhacking.net documents 170 and 171, which describe an early
version) and creaothceann's addendum on the fields added since (document 190),
checked against the Chrono Trigger states of the emuWorks archive. Everything
is little-endian; offsets are into the file. The implementation is
`runtime/snes/zst.c`.

| Offset | Size | What | Checked by |
|---|---|---|---|
| `$00` | 28 | `ZSNES Save State File V143`, `$1A`, `$8F` (143) | all files |
| `$1C`, `$1D` | 1, 2 | cycles left in the line, scanline: always 0 and 224 | all files |
| `$24` | 1 | sound enabled (the SPC700 section below is present) | |
| `$29`-`$38` | | 65816: A (2), DB, PB, S (2), D (2), X (2), Y (2), P, E, PC (2) | PC at the game's wait loops (`$C0:EC71`, `$CD:3E87`); with PC after `LDA $0152`, A is `$7E:0152` |
| `$4A`, `$4C` | 1, 2 | `$4200` NMITIMEN, V-timer | |
| `$4E`, `$50` | 1, 1 | brightness, force blank (`$80`) | |
| `$51`, `$59`, `$5A` | 4, 1, 1 | OBSEL: name base, small and large size as (pixels / 8)² | |
| `$61`, `$63` | 2, 2 | OAM address, OAM address reload | |
| `$66`-`$6A` | 5 | BG mode, BG3 priority, tile size bits, mosaic enable, mosaic size | |
| `$6B`, `$8B`, `$8F` | 8, 4, 8 | per BG: tilemap VRAM byte address, map size, tile data VRAM byte address | tilemaps at `$C000`-`$F000` hold tile words |
| `$97`, `$A1` | 10, 8 | BG1-4 H scroll (and a spare word), V scroll | |
| `$A9`-`$B1` | | VRAM step, VRAM address (dword, byte address), CGRAM address | |
| `$B4` | 2 | main and sub screen designation (`$212C`, `$212D`) | |
| `$B9`-`$BF` | | WRMPYA, WRDIV, RDDIV, RDMPY | |
| `$C7`-`$D4` | 14 | WH0-WH3, window selects as nybbles (BG1-4, OBJ, color), WBGLOG, WOBJLOG, TMW, TSW | |
| `$D5`-`$E1` | 13 | M7SEL, Mode 7 A, B, C, D, X, Y | |
| `$E9` | 4 | `$2181` WRAM port address | |
| `$ED` | 129 | `$4300`-`$437F` as written (16 per channel, then a spare byte) | modes, B-bus targets and table addresses look like HDMA setups; `$43xB`-`$43xF` are `$FF` |
| `$170` | 1 | HDMA channels still active in the frame | |
| `$171` | 152 | ZSNES's HDMA state (host pointers, count, table address) | |
| `$20A`-`$20F` | 6 | fixed color red, green, blue; CGWSEL, CGADSUB | |
| `$218` | 1024 | OAM (544 used) | sprite entries |
| `$618` | 512 | CGRAM | palettes (also found by nesdev forum users) |
| `$A18`, `$A27` | 1, 2 | VMAIN as written, H-timer | |
| `$C13` | 131072 | WRAM `$7E:0000`-`$7F:FFFF` | CPU registers, battle HP at `$7E:5E30`, sound command mirror `$1E00` |
| `$20C13` | 65536 | VRAM | |
| `$30C13` | 65536 + 16 | sound RAM (with `$F0`-`$FF`: `$F1` control, `$F2` DSP address, `$F4`-`$F7` what the CPU wrote, `$FA`-`$FC` timer targets, `$FD`-`$FF` counters) | the driver's code at PC; `$F4`-`$F7` equal the game's command mirror `$7E:1E00` |
| `$40C23` | 36 | SPC700 dwords: PC, A, X, Y, PSW, NZ, SP, direct page, cycles | PC at the driver's main loop (`CALL $0B96`) |
| `$40C47` | 4 | ports as the main CPU reads them | |
| `$40C4B` | 8 | timers enabled, ticks per count (3), ticks left (3), phase bit | |
| `$40C53` | 256 | spare | |
| `$40D53` | 1068 | ZSNES's DSP voice model; at `$40DEB` each voice's next BRR block, at `$40E0B` its loop block | loop blocks equal the sample directory's |
| `$4117F` | 256 | S-DSP registers (128 used) | pitches equal ZSNES's copies at `$40DD3`; DIR, ESA, EDL plausible |
| `$4127F` | 220 | ZSNES internals | |
| `$4135B` | 8192 | cartridge SRAM (size of the cartridge's) | equals the archive's `.srm` where it was saved at the same time |
| `$4335B` | 7168 | 64x56 thumbnail | file size |

Most PPU state is kept decoded, not as register values, and some write-only
state isn't kept at all. The importer rebuilds the registers from it.

### Import

After power-on the importer overwrites the machine:

- **Memory:** WRAM, SRAM, VRAM, CGRAM, OAM and sound RAM are copied.
- **CPU:** registers from the file, not waiting or stopped.
- **PPU:** register values are reassembled from ZSNES's fields and written
  through `ppu_write` (`$2100`-`$2103`, `$2105`-`$2117`, `$211A`-`$2121`,
  `$2123`-`$2133`; the two-byte ones low byte first through their latches,
  Mode 7 before BG1's scroll), so decoded scroll and Mode 7 values come out as
  a game's writes would leave them. Then the write latches, `$2122` and `$213B`
  flip-flops and counter latches are reset, and SETINI is 0.
- **CPU I/O:** NMITIMEN, H/V timers, multiply/divide registers, the WRAM
  port address and all `$43x0`-`$43xF` registers are set directly. HDMAEN is
  taken from the channels still active (usually 0), FastROM is on (Chrono
  Trigger sets `$420D` at boot; the file doesn't hold it), and no DMA or IRQ is
  pending.
- **APU:** the driver phase, SPC700 registers (P from the direct page flag),
  both port directions, `$F1`, `$F2`, timer enables and targets, and DSP
  registers.
- **Position:** the state is taken at the end of line 224, before the NMI
  (RDNMI clear, no NMI in progress). `snes_enter_vblank` puts the timing model
  at the start of line 225, where a frame of ours ends, with RDNMI set and the
  NMI scheduled, so the first frame run starts with the game's NMI handler.

Approximations (what the file holds only in ZSNES's own form, or not at all):

- **S-DSP voices.** A voice with a nonzero ENVX continues from the BRR block
  ZSNES had reached, at envelope ENVX × 16, in sustain (decay if ADSR and above
  the sustain level; release if KOFF is set), with an empty decode history and
  pitch counter; the others are silent. The global rate counter, noise, echo
  position and FIR history start over. Music continues with, at most, a click
  at the import; the next key-ons are exact.
- **Timers.** Stage is the target less ZSNES's ticks left, the divider starts
  at 0, counters keep their low 4 bits.
- **Timing within the line and frame.** The partial line and HDMA progress are
  dropped; ZSNES's CPU/APU cycle balance is lost (the APU starts in step).
- **PPU fields the file doesn't show clearly:** OBSEL's name base and the
  mosaic size (0 in every state checked), OAM priority rotation (off), CGRAM
  address flip-flop. Chrono Trigger's NMI handlers rewrite INIDISP, HDMAEN and
  the scroll registers from WRAM every frame, so these last one frame at most.
- **Not imported:** controller latches, open bus, `$4201` WRIO (stays at power-on),
  coprocessor data (Chrono Trigger has none).

### Checked on the emuWorks states

All 51 Chrono Trigger states of the archive's endings (ct-state15 slots 1-7,
ct-state16-26 slots 1-4) and five story states (ct-state1, 5, 9, 13, 14) were
run from the import for 3600 frames with `--native off` and no input, with a
screenshot every 60 frames. Every one kept running: field scenes animate
(fountains, water, the party idling), the Lavos battles go on (the shell's
imitated bosses, Lavos Core casting, the party's gauges filling, battle
messages), the events before each core fight play their cutscene up to the
first dialog waiting for a button, and the ending states play their opening
(the time vortex, the flash, the first scenes) with music. The first frame
shows the old OAM until the game's NMI handler uploads it.

The twelve ending states (ct-state15 slot 7, ct-state16-26 slot 4) were then
run for 30000 frames with A pressed for 3 frames every 90, once with `--native
off --coverage --calls` and once with `--native on --generated only --misses`
(same screenshots at every 3000th frame in both):

| State | Ending | Result |
|---|---|---|
| 15, 7 | Beyond Time | the pardon, the Moonlight Parade; then waits for the player to walk (the rest is in `endings-beyond-time.ctm`) |
| 16, 4 | Dream Project | reaches the End of Time scene, then waits for the player to walk |
| 17, 4 | The Successor of Guardia | reaches Leene's Square, then waits for the player to walk |
| 18, 4 | Good Night | staff credits to The End (animated) |
| 19, 4 | The Legendary Hero | credits with scenes to The End |
| 20, 4 | The Unknown Past | credits with scenes to The End |
| 21, 4 | People of the Times | credits to The End |
| 22, 4 | The Oath | scenes, credits, The End over a moonlit tower |
| 23, 4 | Dino Age | credits with scenes to The End |
| 24, 4 | What the Prophet Seeks | credits to The End |
| 25, 4 | Slide Show | the slide show and The End |
| 26, 4 | Reunion | reaches the End of Time scene, then waits for the player to walk |

The miss reports were empty: the generated code (23 coverage files, including
`endings-beyond-time`'s) already runs every instruction of all twelve. The
coverage files add 455 instructions (all ROM, 427 in bank `$C0`, 26 in `$FD`)
and the call logs 28 edges to the 23 existing files, from the endings other
than Beyond Time and Dream Project.

## Finding routes

Movies are extended from a save state at their last frame
(`ct_dev --save-state`), trying input from there and checking screenshots. When
the way isn't obvious, read the map instead of guessing: `$7E:7040` holds each
tile's walkability ($01 walkable, $81 blocked; index `(Y << 8 | X) & $1D1E`),
object slots hold tile positions in `$1801`/`$1881`, and the party leader's
slot is `$97` (direct page `$0100` in field mode). The west exit to the robot's
stage was found that way, after the north stairs turned out to be blocked by
two people standing on the only walkable tiles. Walking takes 16 frames per
tile.

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
`--load-state` resumes from it (same build only), which makes iterating on the
end of a long movie fast. Frame numbers and movie input continue from the
saved frame, and resumed runs are identical to straight runs. `--wav out.wav`
writes the sound (32000 Hz stereo, see S-DSP below). `--io-log out.txt` counts
every hardware register access by what made it: a hand port and the platform
service call it went through, a generated block, the interpreter's instruction,
HDMA, or a DMA transfer ([PLATFORM.md](PLATFORM.md)).

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
| `runtime/port/recomp.h` | Instruction semantics for generated code ([RECOMP.md](RECOMP.md)) | SingleStepTests, 5.12M cases with cycle counts; the interpreter on the SNES bus |

Build the conformance runners with the same CMake project (`sst_runner`,
`spc700_runner`); test data comes from `tests/cpu/fetch_sst.py` and
`tests/apu/fetch_sst_spc700.py`. `dsp_test` needs no data: it checks the
envelope rate counter, BRR decoding, and key-on, envelope, ENDX and echo
timing, and must print "all checks passed". `sst_recomp_runner` (same CPU test
data) and `recomp_bus_diff` check the static recompiler's instruction
translation ([RECOMP.md](RECOMP.md)); they're built when Python 3.11+ is found.

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
    always runs; WAI polls on every idle cycle; native code standing in for a
    routine counts as one step whose last cycle is its final 6 clocks.

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
  interrupt timing fields.
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
30.3 s against 29.5 s). Its state is part of the machine image, so save states
from before it (state version 1) don't load.

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
frames match Snes9x with no offset. Over frames 1800-5400, 3212 of 3233
non-blank frames are pixel-identical to the same Snes9x frame. In the dialog
section (5400-7200), 1354 of 1550 match, most of the rest within a frame or
three. WRAM there differs only in frame-count and animation-timer bytes left
over from the earlier timing offset.

Longer movies build on it. All were timed on the development system and reach
the same scenes on Snes9x:

| Movie | Reaches | Frames |
|---|---|---|
| `field.ctm` | walks out of the starting room, down the stairs (map change), through a dialog and a second name entry, out the door onto the world map | 11400 |
| `menu.ctm` | from the starting room, opens the main menu, enters and leaves five submenus, closes it | 8900 |
| `fair.ctm` | `field.ctm`'s route, then across the world map into the fair and north through it | 12450 |
| `fair-companion.ctm` | `fair.ctm`'s route (without its last walk), then up to the plaza, the meeting event with the story character, picking up and returning the item they drop, and the character joining the party | 17900 |
| `gato-battle.ctm` | `fair-companion.ctm`, then out of the plaza's west edge to the singing robot's stage, the conversation that starts the fight, and the first battle, won (results at 21200 here, 21600 on Snes9x: battles run on real time, so they diverge in detail) | 21600 |

Movies in `oracle/movies/saves/` start from power-on with a cartridge save
loaded (`--sram`, see [ORACLE.md](ORACLE.md)) to reach later parts of the game:
bosses, late-game techs, menus, shops, the Epoch, minigames and events. The
saves are the "SRM" set at fantasyanime.com/squaresoft/ctsaves-srm.htm; each
movie's header names the archive and slot and carries the save's CRC-32, and
the frame count to run. They were timed on the development system. Put a
save at `roms/saves/<archive name>/chrono_trigger.srm` and run, for example:

```sh
build/main/ct_dev --rom roms/ct-usa.sfc --sram roms/saves/ctsave07-azala/chrono_trigger.srm \
    --movie oracle/movies/saves/battle-magus.ctm --frames 18000 --native shadow
```

| Movie | Save, slot | Covers | Frames |
|---|---|---|---|
| `battle-magus.ctm` | ctsave07, 1 | Vamp ambush won (results), the Magus fight: Mid Tonic, Ice Water dual tech, Lightning2, barrier changes, a Revive, Cure | 18000 |
| `battle-golem-twins-gameover.ctm` | ctsave10, 3 | Delta Force, the Golems copying elements, Life revive, party wiped, game over back to the title (the sound driver restarts the SPC700 boot ROM) | 12400 |
| `battle-tyrano-lair-walls.ctm` | ctsave08, 1 | Volcano and Avian Rex: Volt Bite, Haste, won with an item drop and a level-up | 8200 |
| `late-omen-battles.ctm` | ctsave15, 1 | Black Omen random battles won (results), status Chaos, many single techs, running away from an ambush | 17100 |
| `late-omen-terramutant.ctm` | ctsave15, 2 | TerraMutant (two-part boss): Delta Force, Mega Bomb, item menus, Life 2 | 15500 |
| `late-omen-zeal.ctm` | ctsave15, 3 | Queen Zeal won (Luminaire, Antipode 3, Flare, Hallation), then the Mammon Machine won (Mega Bomb, Delta Force, Flare and more) and the start of the Zeal face-and-hands battle | 35300 |
| `late-lavos.ctm` | ctsave16, 1 | Lavos: the outer shell beaten with many techs and MegaElixirs, then the core form with a revive | 51000 |
| `events-jetbike-race.ctm` | ctsave03, 1 | Lab 32: Johnny's challenge and the Mode 7 race with turbo boosts | 9400 |
| `events-end-of-time-gate.ctm` | ctsave03, 3 | End of Time pillar, yes/no prompt, gate effect, arrival in Medina | 5600 |
| `events-prison-escape.ctm` | ctsave01, 3 | Prison countdown captions, the march to the guillotine, Lucca's rescue | 12900 |
| `events-keepers-dome.ctm` | ctsave09, 1 | Keeper's Dome: the pendant opens the sealed door, Belthasar's hall | 5000 |
| `events-blackbird.ctm` | ctsave11, 1 | Blackbird escape onto the hull over scrolling clouds | 4400 |
| `menus-field-menu.ctm` | ctsave14, 3 | Every field submenu: equipment, items (use, organize), techs, config, party order, saving | 10600 |
| `menus-shop-inn.ctm` | ctsave14, 1 | Truce Market buy and sell, resting at the inn | 9700 |
| `menus-epoch.ctm` | ctsave14, 1 | Epoch: Mode 7 flight, map view, time travel between eras, landing, buildings | 8800 |
| `menus-end-of-time.ctm` | ctsave12, 2 | Party exchange, the Guru of Time, saving over a slot | 8800 |

All run in shadow mode with no mismatches. Making them found two hangs in the
development system, both fixed: the location-change vblank wait (see Interrupt
timing) and the SPC700's return to its boot ROM after a game over.

Non-blank frames pixel-identical to Snes9x at the same frame / within 16 frames:

| Section | `menu` | `field` | `fair` |
|---|---|---|---|
| 7200-8899, menu | 1264 / 1453 of 1640 | | |
| 7200-8999, walking the house | | 166 / 482 of 1752 | same as `field` |
| 9000-10799, dialog and name entry | | 48 / 185 of 1692 | same as `field` |
| 10800-11399, world map | | 216 / 600 of 600 | |
| 10800-12449, world map and fair | | | 381 / 984 of 1558 |

The house frames that match no Snes9x frame look the same but differ in a few
hundred pixels around the characters (for example 87 pixels in rows 97-112 at
frame 8700). WRAM there differs in about 160 bytes, including a coordinate
byte off by one (`$7E:01AB`), and no frame offset lines it up. Direction holds
land one frame apart relative to the game's frame counter on the two systems,
so positions drift by a pixel. The world map lines up again at a wall. This
looks like input phase, not a hardware-model bug. Movies meant for exact
comparison should walk into obstacles to realign positions.

`fair-companion.ctm`, same metric:

| Section | Result |
|---|---|
| 12200-13899, fair and plaza (walking, waiting) | 137 / 146 of 1656 |
| 13900-15899, meeting event, item pickup and return | 426 / 1302 of 2000 |
| 15900-17899, dialog and joining | 835 / 1696 of 1940 |

Most event frames match within a few frames, so both systems run the same
events; the low same-frame rate while walking looks like the position drift
above but wasn't investigated separately. Two spots were checked by
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

1. **Battle.** `gato-battle.ctm` reaches and wins the first battle. The battle
   system (banks `$C1`, `$CC`, `$CD`) runs entirely on the interpreter so far;
   it's the next large area to port, now that a movie exercises it.
2. **Porting.** Continue replacing routines with native code; shadow mode over
   `new-game`, `field`, `menu`, `fair`, `fair-companion` and `gato-battle` is
   the regression check (docs/PORTING.md).

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

# Open follow-ups

Loose ends the milestone docs record in passing, collected so they don't get
lost. Each line says where the detail lives. Milestones themselves are in
[APPROACH.md](APPROACH.md), [RECOMP.md](RECOMP.md), [MATCHING.md](MATCHING.md)
and [PLATFORM.md](PLATFORM.md).

## Correctness and accuracy

- **Hand ports' time is an estimate**, so `--native on --recomp on` drifts from
  the original (battles play out differently). Generated-only mode is exact.
  Two approaches tried and rejected are in RECOMP.md, "M2"; the real fix is
  either M5 (ports derived from generated blocks) or PLATFORM's backends, where
  SNES-exact timing stops mattering.
- **IRQ timing simplifications** (RUNTIME.md, "Interrupt timing"): hardware's
  ~3.5-dot trigger delay isn't modeled, and an IRQ pending across `CLI` is
  taken one instruction early.
- **S-DSP** is sample-granular, and note starts differ from Snes9x by one KON
  poll (RUNTIME.md, "S-DSP"). The audio comparison table predates the DMA and
  refresh timing fix and should be re-measured.
- **Big-endian hosts** are untested (CMake warns); save states are raw memory
  images.
- **ZSNES state import** approximates DSP voice history, timer phase and
  in-frame position (RUNTIME.md, "ZSNES save states"), so those runs are for
  coverage only.
- **`ct_fuzz` doesn't use generated blocks**, only hand ports (RECOMP.md).

## Recompiler

- **M4**: skip flags nobody reads, call generated blocks directly where it's
  safe. Speed only; rendering and sound dominate now.
- **M5**: a workflow that turns a generated block into a readable hand port,
  which is what PLATFORM's later milestones need.
- **Player without the interpreter**: the strict build's CI check is new and
  hasn't run on a real miss; the misses-feedback loop is documented but only
  exercised by hand so far (PLAYING.md, "Without the interpreter").

## Matching disassembly

- **MD2 leftovers** (MATCHING.md, "MD2 as built"): `$F6:24B1-7FFF` and other
  gaps (43481 census-unknown bytes), plus 322.5 KiB still typed `mixed`.
  The D0 graphics pool, CF 2bpp graphics, E4 sprite records and E5 exit lists
  now have codecs; tile-descriptor ownership and the E4 battle-record count
  still need stronger bounds. Not started: the string encoding, the
  event/battle-animation/enemy script formats as typed regions with round
  trips, and re-encoding for `lz` (today's check is that the decompressor
  consumes the bytes exactly). The data-typing follow-up still needs the
  full movie suite before integration.
- **MD3**: every pointer symbolic, plus the static and dynamic shift tests.
- **MD4 residue**: 96 jump tables are bounded by layout alone, the claim that
  menu modes 8-10 and 15 are never selected is assumed, and C4 (RAM code) rests
  on the enumerated sections plus the miss report.
- **asar cross-check** of the generated source has never run (asar isn't
  installed here); the syntax points it would settle are listed in MATCHING.md.

## Platform

- **P4** (widescreen) and **P5** (GBA feasibility) are next; P2 and P3 are done
  but for the leftovers below.
- **P2 leftovers** (PLATFORM.md, "P2 as built"): the jet bike race's own display
  code, 19 blocks between `$7E:36DA` and `$7E:8752` that only
  `events-jetbike-race` reaches, found from the I/O log late; `$7E:59D5`, where
  the graphics loader starts a fill without programming a source, so the
  transfer reads on from wherever the last one left the channel and no call can
  name it; and `$D1:F4E6`, reached once on `battle-golem-twins-gameover`'s path
  back to the title. Until they are behind the API, `--vid-check` from power-on
  is still not possible (1265 of `new-game`'s first 3000 frames) and
  `--vid-check-from` remains how a subsystem is checked.
- **P3 leftovers** (PLATFORM.md, "P3 as built"): the observer doesn't fill in
  the per-span memory pointers the decoder now does, which it needs before the
  renderer can be fed by service calls rather than registers. The native
  renderer is a wash against `render.c` rather than faster (the milestone's 81%
  figure predated `render.c`'s line cache), so the speed case for it is still
  unmade; what remains to win is the decoder, which a call-fed build wouldn't
  run at all.
- **Open questions** in PLATFORM.md: code that stops when the beam passes a
  scanline (the event scheduler `$C0:5A46`, the sprite frame queue `$C0:CA76`),
  battles playing out differently once timing changes, widescreen's exposed
  assumptions (the 0-255 object window at `$C0:A88D`, map data beyond VRAM,
  single-byte battle window positions), and GBA's constraints.
- **P1 leftovers**: `$217F` mirror writes still come from unported video code,
  and the APU-direct backend's audio comparison misses one measure on
  `new-game` (PLATFORM.md, "P1 as built").

## Movies and coverage

- **Endings**: Reunion needs a route to Lavos from "The Time Egg" (no Epoch
  reachable from that save); nine more endings are unrecorded but cheap now
  (MOVIES.md).
- **zophar `chrono-7`** (everything maxed at the fair) is unused: its Lucca
  exhibit gate (`$7F:0055` bit 7) can be opened, but no movie needs it yet.
- **Coverage loop**: re-run `--misses` over the whole library after the latest
  movies and feed anything new back into the build.
- **Snes9x agreement tables** in RUNTIME.md were re-measured after the timing
  fix for `new-game`, `menu` and `field` only; `fair` and `fair-companion` got
  worse and are unexplained.

## Player and tooling

- Gamepad support, configurable keys, and quick saves that survive a rebuild.
- CI: the macOS job costs 10x minutes on a private repo.

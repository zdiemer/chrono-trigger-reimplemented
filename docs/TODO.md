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

- **MD2 leftovers** (MATCHING.md, "MD2 as built"): `$D0:0000-DFFF` (57 KiB, no
  reader found), `$F6:24B1-7FFF`, the `$E4:F000`/`$E4:F600` record tables,
  `$E5:0000-1FFF`, and 379 KiB still typed `mixed`. Not started: the string
  encoding, the event/battle-animation/enemy script formats as typed regions
  with round trips, and re-encoding for `lz` (today's check is that the
  decompressor consumes the bytes exactly).
- **MD3**: every pointer symbolic, plus the static and dynamic shift tests.
- **MD4 residue**: 96 jump tables are bounded by layout alone, the claim that
  menu modes 8-10 and 15 are never selected is assumed, and C4 (RAM code) rests
  on the enumerated sections plus the miss report.
- **asar cross-check** of the generated source has never run (asar isn't
  installed here); the syntax points it would settle are listed in MATCHING.md.

## Platform

- **P2** (video behind the API) is in progress; **P3** (native renderer from
  `VidFrame`), **P4** (widescreen) and **P5** (GBA feasibility) follow.
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
- CI: actions still on Node 20 (GitHub warns), and the macOS job costs 10x
  minutes on a private repo.

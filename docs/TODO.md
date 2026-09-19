# Open follow-ups

Loose ends the milestone docs record in passing, collected so they don't get
lost. Each line says where the detail lives. Milestones themselves are in
[APPROACH.md](APPROACH.md), [RECOMP.md](RECOMP.md), [MATCHING.md](MATCHING.md)
and [PLATFORM.md](PLATFORM.md); which of these to work on next, and why the
rest waits, is [PLAN.md](PLAN.md).

## Correctness and accuracy

- **Hand ports' time is an estimate**, so `--native on --recomp on` drifts from
  the original (battles play out differently). That difference is accepted
  (PLAN.md, Phase 2's timing decision); the timing gate bounds the errors
  (RECOMP.md, "The timing gate"). Left open there: six entries whose time
  follows their data are on `tools/timing_exceptions.txt` (the decompressor,
  menu text and glyphs, `field_rasterize_quad`, `menu_object_wave_hdma`);
  and adding a movie or a port means recalibrating, since the
  drift limit holds only for the measured value. `--recomp only` stays exact.
- **Fuzzing coverage of existing ports.** Fuzzing is now part of every port's
  definition of done (PORTING.md); ports registered before that without a
  recorded fuzz run should get one, subsystem by subsystem in Phase 3.
- **IRQ timing simplifications** (RUNTIME.md, "Interrupt timing"): hardware's
  ~3.5-dot trigger delay isn't modeled, and an IRQ pending across `CLI` is
  taken one instruction early.
- **S-DSP** is sample-granular, and note starts differ from Snes9x by one KON
  poll (RUNTIME.md, "S-DSP"). The audio comparison table predates the DMA and
  refresh timing fix and should be re-measured.
- **Big-endian hosts** are untested (CMake warns).
- **Packaging on Linux and macOS** isn't checked on a machine that never built
  it, and `dist/` there relies on the player's machine having SDL2 (PLAYING.md,
  "Build"). A macOS bundle would copy the dylib and rewrite its install name.
- **ZSNES state import** approximates DSP voice history, timer phase and
  in-frame position (RUNTIME.md, "ZSNES save states"), so those runs are for
  coverage only.
- **`ct_fuzz` doesn't use generated blocks**, only hand ports (RECOMP.md).

## Recompiler

- **M4**: skip flags nobody reads, call generated blocks directly where it's
  safe. Speed only; rendering and sound dominate now.
- **M5**: a workflow that turns a generated block into a readable hand port,
  which is what PLATFORM's later milestones need.
- **Gaps in the vblank check** (RECOMP.md, "Where video writes land"), which
  the suite runs with four findings accepted (RECOMP.md, "The timing gate"): a
  hand port's region ends at its last service call, so the reference's work in
  code the original jumps to past it is `unmatched` (the menu loads);
  following the call stack in the reference would close that. And it names the
  routine whose writes land late, not the port whose time made them late.
- **Player without the interpreter**: the strict build's CI check is new and
  hasn't run on a real miss; the misses-feedback loop is documented but only
  exercised by hand so far (PLAYING.md, "Without the interpreter").

## Matching disassembly

- **MD2 leftovers** (MATCHING.md, "MD2 as built"): `$F6:24B1-7FFF` and other
  gaps (42590 census-unknown bytes), plus 228.3 KiB still typed `mixed`.
  The D0 graphics pool, CF 2bpp graphics, E4 sprite records and E5 exit lists
  now have codecs; tile-descriptor ownership and the E4 battle-record count
  still need stronger bounds. The strings' leftovers (MATCHING.md, "The string
  encodings"): the dialog tables at `$FF:D3FE` and `$CC:3A09`, whose extents
  aren't settled, and the menu strings passed to `$C2:EF65` by address in code.
  The event scripts' leftovers (MATCHING.md, "The event scripts"): commands
  and operands named mechanically (`set_1c80`, `party_routine_6`), and the 39
  command layouts read from handlers no port has checked yet.
  The battle scripts' leftovers (MATCHING.md, "The battle script formats"):
  names for the commands, conditions and actions that keep their numbers, and
  deriving the action and target tables (`$C1:B85F`, `B88D`, `B8BB`) from the
  parse. Not started: re-encoding for `lz` (today's check is that the
  decompressor consumes the bytes exactly; the event scripts, all `lz` streams,
  round-trip on their decompressed output and would be assembled from their
  decoded form with it: MATCHING.md, "The event scripts").
- **The RAM map's leftovers** (DISASSEMBLY.md, "The RAM map"): 32 addresses
  the code touches without anything establishing what they mean (menu 13, field
  6, battle 5, event 4, scene 4, core 2), named as Phase 3 reaches each
  subsystem; and the menus' two modes, which reuse the same bytes while the map
  allows one owner per subsystem, so the object mode's slots at `$0B30` and part
  of its page-0 cells are unnamed until the menus get a second subsystem tag.
- **MD3**: every pointer symbolic, plus the static and dynamic shift tests.
- **MD4 residue**: 90 jump tables are bounded by layout alone, the claim that
  menu modes 8-10 and 15 are never selected is assumed, and C4 (RAM code) rests
  on the enumerated sections plus the miss report.
- **asar cross-check** of the generated source has never run (asar isn't
  installed here); the syntax points it would settle are listed in MATCHING.md.

## Platform

- **P4** (widescreen) and **P5** (GBA feasibility) are next; P2 and P3 are done
  but for the leftovers below.
- **The Epoch's flight screen** (PLATFORM.md, "P2 as built"), found once
  `--vid-check` could run from power-on: about fifteen routines between
  `$C2:6325` and `$C2:76A8` program their own tilemaps, sprite bases, uploads and
  HDMA channels, and `menus-epoch` agrees on 5505 of its 8799 frames because of
  it. `menus-end-of-time` has a smaller one, `bg.map_size` on 565 frames.
  Neither movie was in the set the menus were measured on, which is why neither
  had been looked at. Same shape of work as the race: an I/O log with
  `--recomp off` names every writer, then one entry each.
- **P3 leftovers** (PLATFORM.md, "P3 as built"): the observer doesn't fill in
  the per-span memory pointers the decoder now does, which it needs before the
  renderer can be fed by service calls rather than registers. The native
  renderer is a wash against `render.c` rather than faster (the milestone's 81%
  figure predated `render.c`'s line cache), so the speed case for it is still
  unmade; what remains to win is the decoder, which a call-fed build wouldn't
  run at all.
- **Host backends beyond SDL**, none of them scoped as milestones yet. Game code
  reaches the host only through the platform services, so each is a backend
  behind an existing API rather than a change to `game/`:
  - **Rendering on the GPU** (Vulkan, Metal or GL). It would be a third consumer
    of `VidFrame` beside `render.c` and P3's renderer, with `--px-check` as its
    conformance harness. What it implements is SNES compositing, not arbitrary
    geometry: per-line state (`VidSpan`) makes a frame scanline-shaped rather
    than a few quads, and the colour math has to stay integer to keep
    pixel-identity as the test oracle. It wants feeding from the observer rather
    than the decoder, so it waits on the P2 and P3 leftovers above.
  - **Audio.** Output is already SDL2's audio device, which covers WASAPI,
    CoreAudio and ALSA/PipeWire, so another output layer buys little. The real
    item is a native mixer behind `au_*` replacing the SPC700 driver, which
    APPROACH.md calls optional and later; `--audio direct` is the halfway house.
  - **Controllers.** `in_host_set` is already the one host entry point per
    frame, so this is an SDL GameController call into it plus bindings (see
    Player and tooling).
- **Assets as data files rather than ROM reads.** The matching build already
  extracts every typed data region from the user's ROM (MATCHING.md,
  "Build-time extraction"): raw blobs, and decoded forms where a type has one -
  decompressed streams, BRR as is, graphics as planar tiles. Three things are
  missing before that is an asset pipeline. A layout meant to be *read* rather
  than reassembled, and stable enough to depend on. The script as text: the
  strings and the event scripts that name them decode to a readable form now
  (MATCHING.md, "The string encodings" and "The event scripts"), the battle
  scripts don't. And a decision about whether the runtime
  should load from such a directory at all, since APPROACH.md's conventions
  have assets loading from the ROM image at run time and every reader today
  assumes that. A GBA build is what forces the question, because it can't map a
  4 MiB SNES image the way the recompilation does; widescreen and translation
  want it too. Extracted assets are ROM-derived and stay out of the repository
  wherever they live (CLAUDE.md), so a user data directory beside the saves is
  the shape.
- **Open questions** in PLATFORM.md: code that stops when the beam passes a
  scanline (the event scheduler `$C0:5A46`, the sprite frame queue `$C0:CA76`),
  and code that races the NMI handler for the vblank flag (`$C0:3B71` and
  `$C0:4181`, idle entries that wait for vblank instead; RECOMP.md, "The timing
  gate": the shipping mode lost that race once and hung a route),
  battles playing out differently once timing changes, widescreen's exposed
  assumptions (the 0-255 object window at `$C0:A88D`, map data beyond VRAM,
  single-byte battle window positions), and GBA's constraints.
- **P1 leftovers**: `$217F` mirror writes still come from unported video code,
  and the APU-direct backend's audio comparison misses one measure on
  `new-game` (PLATFORM.md, "P1 as built").

## Targets beyond the desktop SNES build

Wanted, none of them scoped as milestones. Each says what the design already
carries and what it doesn't; what they share is that the game side is still 85%
generated code (PLAN.md, Phase 3), so anything needing the *game* to know
something -- a screen coordinate, a menu's shape, where a resource lives --
waits on M5 rather than on a backend.

- **A WebAssembly build.** The seam is right: `game/` reaches the host only
  through `runtime/platform/`, and the one frontend (`frontend/ct_play.c`, SDL2)
  is what Emscripten ports best. Four things are in the way, in order of how
  much they cost.
  - **Call-outs run on their own stack.** Every non-idle registry entry runs on
    a fiber (`runtime/port/fiber.c`, `job_run` in `runtime/port/native.c`), which
    is Windows fibers or POSIX `ucontext`; wasm has neither. The abstraction is
    already in the right place, so the question is what can sit behind it:
    Asyncify (a whole-program cost), JSPI (cheaper, but a floor on what can run
    the build), or a call-out that doesn't suspend its caller, which is a
    `native.c` redesign. This decides whether the build is easy or a project --
    but only for a build that runs hand ports. Generated blocks run on the
    machine without a fiber (they never call out; RECOMP.md), and
    `--recomp only` runs no hand ports, so a build the user compiles from their
    own ROM in that mode needs no stack switching at all. The question is live
    for the public build below, and for the default player, which runs hand
    ports since the timing gate made `--recomp on` its default.
    The spike, and why no phase covers it, is in PLAN.md's "What this plan
    deprioritizes".
  - **What the browser build is made of.** The default path generates C from the
    user's ROM at build time (RECOMP.md), and nothing ROM-derived can be shipped
    (CLAUDE.md), so a public build cannot contain it. That leaves a build the
    user compiles, or one that is hand ports plus the interpreter over a ROM the
    page loads at run time -- which makes M5's coverage the gate on it, and asks
    the same question as "assets as data files" above: the readers assume a
    mapped ROM image.
  - **A driver of its own.** The player advances one frame per call and hands
    out what it persists as bytes under names (PLAYING.md, "How the player is
    built"), so what the browser needs is a third driver beside `run_window`
    and `run_headless`: a frame callback, an audio worklet fed through
    `audio_fill`, and saves kept in browser storage. The rules that keep those
    seams from re-closing are in CLAUDE.md, "Writing host-side code".
  - **Speed**, which is unmeasured: the interpreter plus `render.c` per line, in
    one thread, with no `-static` build to lean on.
- **A native AYN Thor multi-screen port.** Two parts, and only one of them is a
  backend. The Android/ARM64 side is ordinary: SDL2 builds there, no screen
  geometry is baked into `game/` (PLAN.md, Phase 2), and the handheld's pad
  arrives through `in_host_set` like any other (a binding in its driver, as
  `frontend/bindings.c` is the desktop's). The second
  screen is the real question, because `VidFrame` is one frame of one screen per
  frame. Two shapes, and they cost differently: a *companion* view (party state,
  the map, an item list) drawn by the frontend from game state, which can read
  it by name from the RAM map (`re/ram.toml`, `game/ram.h`); or a
  *second game screen* in the DS sense, which needs the video service to carry
  more than one frame and the game to decide what goes where. Either way it wants
  P3's leftover first (the observer doesn't fill in the per-span memory pointers),
  since a frontend-fed screen is the same call-fed rendering widescreen needs.
  Native resolution is far above 256x224, so it also meets P4's questions about
  what lives outside the original screen.
- **DS and PlayStation content in the original.** The Dimensional Vortex, the
  Arena, the Lost Sanctum, the bestiary: this is content authoring, not a port,
  and it is the largest item in this file by a wide margin. What it needs, in
  dependency order: the scripts and text are typed with round trips now
  (MATCHING.md, "The string encodings", "The event scripts" and "The battle
  script formats"), so what a dungeon needs first is the **asset pipeline**
  ("Assets as data files" above), because
  new maps, sprites and music have no ROM address to read from, which is exactly
  what Phase 3's named resource accessors are the seam for; and the field, event
  and scene subsystems ported far enough to extend (`$C0` is 84% generated and
  `$C6` has no entries at all). Three things are worth writing down now so they
  don't surprise later: the DS and PS1 releases are a different engine and a
  different asset format, so this is re-implementation from a reference, not an
  import; their assets are as ROM-derived as ours and stay out of the repository
  wherever they live, extracted from the user's own copies; and the animated
  cutscenes have no path at all -- there is no video decode anywhere in the
  runtime, and adding one is its own decision.
- **A SNES ROM compiled from the hand ports.** Not byte-exact: a playable ROM
  whose routines are the C in `game/`, compiled for the 65816, with the
  original's assembly for whatever isn't ported. In dependency order:
  - **Relocatable source first.** The matching build rebuilds the ROM
    byte-identically from assembly, but every pointer is still a number, so
    nothing can move or grow. MD3 (every pointer symbolic, with the shift tests)
    is what makes a routine replaceable at all; it is the prerequisite for any
    ROM that differs from the original, compiled or hand-edited.
  - **Then a hybrid, one routine at a time**: link a compiled routine in place of
    the original's, keep the original's assembly for the rest, and check that the
    ROM plays. A call-out to unported code is a real `JSL` there, so the
    incremental shape the project already has carries over.
  - **Ports written for it.** The ports run against an emulated machine: they take
    a `Snes *`, read through `m_read8`, and the transcription-style ones move
    emulated registers about (about 5800 calls to `m_lda`, `m_push`, `set_dp` and
    the like across `game/`). A 65816 build needs each routine's logic apart from
    the original's calling convention, which is also what the readability bar
    asks for; CLAUDE.md's porting rules say so, and `tests/test_port_subset.py`
    keeps `game/` inside what a 65816 compiler handles (no floating point, no
    64-bit integers, no host C library -- none of which `game/` uses today). The
    platform services already fit: their SNES backend turns each `vid_*` call
    into the original register writes, which is what a SNES build compiles them to.
  - **Speed is the open question.** The CPU runs at about 3.6 MHz, the original
    is hand-tuned assembly, and compiled C is several times larger and slower;
    NMI and HDMA work that misses vblank corrupts the picture rather than slowing
    it. Which routines can be C at all is to be measured, not assumed, and the
    hot ones may stay assembly for good. Size too: about 240 KB of code in a
    nearly full 4 MiB ROM, so an extended layout (ExHiROM) is likely.
  - **The toolchain** is its own evaluation: 65816 C compilers exist (PVSnesLib's,
    Calypsi), and they differ in output quality, control over the calling
    convention, and licence.
- **Native touch controls**, rather than a virtual d-pad over the screen. A
  virtual pad is free today and needs nothing new: the frontend maps it to
  `PadBits` and calls `in_host_set`. Real touch is not a host problem, it is a
  game-side one. The input service is one 16-bit word per frame because that is
  what the machine reads; a pointer service beside it (`in_host_point`, position
  and pressed) is additive and cheap. What it buys nothing without is a game that
  knows where its UI *is*: menus and dialogs are drawn as tilemap writes from
  `$C2`, 92% of it still generated, so there is no element with a screen-space
  box to hit-test against. So the order is: the pointer service, then the menu
  and dialog subsystem ported to the bar where a selectable item carries its own
  rectangle, then a policy per mode (a tap on the field: a target to walk to, or
  a synthesized direction -- the object activation window at `$C0:A88D` makes
  that a real question, the same one P4 runs into). Screen coordinates are
  already signed by the porting rules, which is what keeps this from needing a
  second pass after widescreen.

## Movies and coverage

- **Endings**: Reunion needs a route to Lavos from "The Time Egg" (no Epoch
  reachable from that save); nine more endings are unrecorded but cheap now
  (MOVIES.md).
- **zophar `chrono-7`** (everything maxed at the fair) is unused: its Lucca
  exhibit gate (`$7F:0055` bit 7) can be opened, but no movie needs it yet.
- **Coverage loop**: re-run `--misses` over the whole library after the latest
  movies and feed anything new back into the build.
- **Battle setup is never compared.** Every call of `battle_setup_state`
  (`$CC:E2E4`) and `battle_setup_graphics` (`$CC:ED87`) in the library is
  interrupted by NMI, so shadow mode counts them and compares none; the gate
  passes on them without looking. What checks them today is `--native on
  --recomp off` tracing identically to a known-good build (DISASSEMBLY.md,
  "The RAM map"). A fuzz state at their entry, or comparing interrupted calls
  when the interrupt touches none of their state, would close it.
- **Snes9x agreement tables** in RUNTIME.md were re-measured after the timing
  fix for `new-game`, `menu` and `field` only; `fair` and `fair-companion` got
  worse and are unexplained.

## Player and tooling

- **The movie suite's wall time is the longest movie's.** The gate
  (`tools/suite.py`) takes about 10 minutes at `-j 2` (PORTING.md, "What
  shadow mode costs"). The three endings are 54% of its CPU time and
  `endings-bucket-lavos` alone 24%, so from about `-j 4` up the wall time is
  that one movie however many workers are idle:
  - **Split the long movies.** A `--native off` pass (3-4 times faster than
    shadow) could save states at a few cut points, and the segments run in
    shadow in parallel from them, with their coverage and call files merged.
    The routines in flight at a cut would go unchecked there.
  - **Skip the shared prefix.** Every movie replays the attract sequence and
    title from power-on before reaching what it covers, about 1638 frames each
    across 25 movies, now about 5% of the suite. A state snapshot taken once at
    the point a movie's own input starts would cut that, at the cost of no
    longer validating the boot path 25 times over.
  - **Sound RAM in the shadow copies.** A refresh still copies the 90 KB
    outside work RAM and VRAM, mostly the APU's 64 KB, which with the
    comparison is about 15% of an ending's shadow run. Stamping sound RAM
    pages as work RAM's are (the SPC700's writes and the DSP's echo writes)
    would cut it.
  - **A fast gate**: `--movies` and `--only` narrow a run by hand, but deriving
    the subset that can reach the subsystem a change touched would make the
    iterating case automatic, with the full set still required to land.
- CI: the macOS job costs 10x minutes on a private repo.
- **`ct_drive` can't see the time ports owe.** Its replay bases (MOVIES.md,
  "Native code") require no routine suspended, but ON mode's `debt` in
  `native.c` isn't exposed, so a base with time owed makes the replay come out
  "inexact". An accessor in `native.h` (0 at a base) makes every replay exact.
- **`ct_dev` builds its own native table** (for `--exclude`) instead of using
  `frontend/natives.c` as `ct_play` and `ct_drive` do, and its `--recomp only`
  leaves the ported routines out in shadow mode too, where the others keep
  them. Move it onto `natives_select` with an exclude filter.

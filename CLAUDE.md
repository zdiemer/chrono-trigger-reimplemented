# chrono-trigger-recomp

Native C reimplementation of Chrono Trigger (SNES, USA), validated by
per-frame state diffs against the original ROM. Strategy: docs/APPROACH.md.

## Facts

- ROM: No-Intro *Chrono Trigger (USA)*, CRC32 `2D206BF7`. HiROM+FastROM, 4 MiB,
  8 KiB SRAM, no coprocessor. `tools/rom.py stage <path>` verifies it and
  copies it to `roms/ct-usa.sfc` (gitignored).
- Addresses are written in SNES bus form, `$C0:1234`. HiROM file offset =
  `(bank & 0x3F) << 16 | addr` for banks `$C0-$FF` (and `$40-$7D`).

## Commands

- `python3 -m tools.disasm`: static code discovery. Writes `disasm/out/`
  (gitignored). Knowledge goes in `re/hints.toml`. See docs/DISASSEMBLY.md.
- `python3 -m tools.disasm.linear BB:AAAA -n N --m 8 --x 16`: quick look at code.
- `python3 -m unittest discover tests`
- Oracle: `cmake -S oracle -B build/oracle -G Ninja && cmake --build build/oracle`, cores via
  `python3 tools/fetch_core.py`, then `build/oracle/ct_oracle --core cores/snes9x_libretro.<ext>
  --rom roms/ct-usa.sfc --frames N --movie <.ctm> --trace traces/<name>.cttr`. Inspect and diff with
  `python3 tools/trace.py`. Snes9x is the trace oracle. See docs/ORACLE.md.
- Main build: `cmake -S . -B build/main -G Ninja -DCT_ROM=roms/ct-usa.sfc
  "-DCT_RECOMP_COVERAGE=traces/coverage-*.txt;traces/saves/cov-*.txt"
  "-DCT_RECOMP_CALLS=traces/calls-*.txt;traces/saves/calls-*.txt" && cmake --build build/main` builds
  `ct_dev` (ROM on our runtime, writes `.cttr` traces; see docs/RUNTIME.md), `ct_play` (docs/PLAYING.md),
  `ct_fuzz`, the conformance runners, the C generated from the ROM (docs/RECOMP.md) and the matching
  reassembly check (docs/MATCHING.md). Coverage and call files are optional, gitignored inputs.
- CPU tests: `python3 tests/cpu/fetch_sst.py` and `python3 tests/apu/fetch_sst_spc700.py` once, then
  `build/main/sst_runner tests/cpu/data/*.bin`, `build/main/sst_recomp_runner tests/cpu/data/*.bin`,
  `build/main/recomp_bus_diff` and `build/main/spc700_runner tests/apu/data/*.bin` (all must report 0
  failures). `build/main/dsp_test`, `fiber_test`, `state_test`, `shadow_test`, `fuzz_test`, `time_parts_test`, `bindings_test` and `vars_test` need no data.
- CI (`.github/workflows/ci.yml`): Linux gcc and clang, macOS Apple clang, Windows MSYS2 MinGW gcc, all
  configured without a ROM and with `-DCT_WERROR=ON`; runs the unit tests, `dsp_test`, `fiber_test`, `state_test`, `shadow_test`, `fuzz_test`, `time_parts_test`, `bindings_test`, `vars_test`,
  the conformance runners (test data cached) and builds `ct_play` and the oracle. Code must build
  warning-free on all four and tests must pass without a ROM. Docs-only changes don't trigger it.
- Movies: `oracle/movies/*.ctm` from power-on, and `oracle/movies/saves/*.ctm` from cartridge saves
  (`--sram`; each header names its save and frame count; docs/RUNTIME.md). Record new ones with
  `ct_play --record` (docs/PLAYING.md), or script them from game state: `build/main/ct_drive` plus
  `python3 -m tools.movies.routes.<route> --sram <save.srm> --prefix <scratch dir> --movie <out.ctm>`
  (docs/MOVIES.md). Routes survive timing changes; the movies they write don't, so re-run the route.
- `python3 tools/burndown.py`: how much of the ROM's code the hand ports cover, per bank or
  `--by subsystem`, with `--uncovered BANK` listing the largest unported runs. An estimate, for
  sequencing the port work (docs/PLAN.md); its docstring says which way it is wrong. It counts a
  port that transliterates the assembly as done, so read it with `portstyle.py`.
- `python3 tools/portstyle.py`: how much of each port is reimplementation and how much is
  transliterated assembly, per subsystem and per file (`--files`, `--subsystem S`). `--check` is the
  gate's ratchet against `tools/portstyle_baseline.txt`, the files written before the bar existed:
  rewriting one to the bar deletes its line, and nothing may be added (docs/PLAN.md, "Phase 3a").
- `python3 -m tools.recomp.show BB:AAAA... [--m 8 --x 16]`: routines as drafts for their hand ports, to
  stdout, never into the repo. **Ask for every routine of a chunk in one run**: the analysis is solved
  once for the whole run and is nearly all of its time, so four drafts cost the same 27 seconds as one.
  `tools.recomp.draftcheck` shadows drafts in a scratch tree (docs/PORTING.md, "From a draft to a port").
- Validation: `python3 tools/suite.py --ct-dev build/main/ct_dev` runs the gate and exits 0 only when
  it passes (`--only core|saves`, `--movies <name>...` and `-j N` narrow it while iterating). The gate
  is: every movie in `--native shadow` exits 0 with no `mismatched [1-9]` lines, no `page refused` line (a
  guard that turned away a call whose direct page is no instance `re/ram.toml` declares) and no `accessor out
  of range` line (a port that called an accessor with an index past what the map declares, reaching the next
  variable in work RAM; both in docs/DISASSEMBLY.md, "The accessors");
  `--native on --recomp only` produces traces identical to `--native off` (`python3 tools/trace.py
  diff`); every hand port's estimated time stays within the timing gate's limits over the shadow runs
  or is in `tools/timing_exceptions.txt` with a reason (`tools/timing_gate.py`, run by the suite; after
  adding a movie or a port, recalibrate with its `--calibrate game/registry.c`; docs/RECOMP.md, "The
  timing gate"); no video write in the shipping mode (`--recomp on`, `ct_play`'s default) lands on the
  picture where the reference's doesn't, or it is in `tools/vblank_check_exceptions.txt`
  (`tools/vblank_check.py`); the route scripts reach their outcomes in the shipping mode
  (`tools/movies/route_check.py`; the suite runs the fast route, `--long-routes` all of them, which
  is worth doing before a change to timing or to the native runtime lands); every
  don't-care declaration in `game/` is one the liveness analysis agrees with
  (`tools/dontcare_check.py`, run by the suite, static; docs/RECOMP.md, "Before the bulk"); and no
  port transliterates the assembly where one didn't before (`tools/portstyle.py --check`, run by the
  suite, static; docs/PLAN.md, "Phase 3a").
  Two checks the suite doesn't run, so run them by hand when a change could move them:
  `ct_dev --native on --recomp only --misses out.txt` lists anything still interpreted, and
  `ct_dev --px-check` must report every frame identical (the native renderer against `render.c`,
  docs/PLATFORM.md P3).

- Porting: add routines under `game/<subsystem>/` (named by what they do, not by ROM bank), register in
  `game/registry.c`, validate with `ct_dev ... --native shadow` over the movie library (0 mismatches),
  then `--native on`, and fuzz with `build/main/ct_fuzz --load-state <state> --routine <name>`. Hardware
  access in converted files goes through `runtime/platform/` (docs/PLATFORM.md; a lint test enforces
  it). See docs/PORTING.md, and `.claude/skills/port-routine/` for the workflow an agent follows
  (the bar with worked examples, the self-check, the worktree rules, the report). A subsystem merges
  only after a **qualitative review** by a reader who hasn't seen the author's report
  (`.claude/skills/review-port/`): the gate and `portstyle.py` prove behaviour and syntax, and neither
  can tell a hand reimplementation from a translation with good names.

## Rules

- **Writing a port from generated code** (docs/PLAN.md, docs/RECOMP.md's M5). The draft
  `tools.recomp` prints is a mechanical image of the ROM; the port is source, and the later
  goals (a native renderer, widescreen, a GBA or browser build, assets as data, a SNES ROM
  compiled from the ports themselves) are made
  easy or hard here rather than in a backend:
  - **This is a reimplementation, not a transliteration, so the code is expected to
    diverge from the original's shape.** What must match is observable behaviour, which
    the gate measures; nothing requires the port to reach it the way the ROM does. A port
    that still moves an emulated A, X and P about, spells its variables as addresses and
    follows the original's basic blocks has translated the assembly rather than
    reimplemented the routine, however well it passes. Judge a finished port by whether
    someone who has never seen the ROM can read it and say what the game does.
  - **Keep the machine out of the logic.** A function that takes a `Snes *` can only run on
    an emulated SNES, however well it reads. The routine's work goes in functions that take
    the game's own state and plain C values and return results; the translation to and from
    the original's registers, flags and direct page lives in a thin wrapper at the registered
    entry, and that wrapper is the only place `m_*` and the `Cpu` struct appear. Game state
    is reached through the typed accessors generated from `re/ram.toml`, never by address:
    they are RAM-backed while a subsystem is still half-generated, so shadow mode keeps
    comparing bytes, and the storage swaps behind them once it is fully ported
    (docs/PLAN.md, "Phase 3a"). A routine that calls out to still-generated code mid-body
    keeps a machine-aware wrapper around a decoupled core until its callees are ports.
  - **Build a vocabulary and call it.** The routine's work belongs in named functions --
    `draw_window_frame`, `load_slot_summary`, `hide_sprites` -- not in a run of machine
    operations the reader has to simulate. If a port is a long straight line of
    `m_lda16`/`m_write16`/`c->a`, the abstraction it needed hasn't been written yet: write
    it, in the port's own file if it is local, in the subsystem's header if shared, in
    `runtime/platform/` if it is the host. `m_*` and the CPU struct are for a port's thin
    entry wrapper and for what genuinely has no better expression yet, not for its body.
  - **The accessors are the storage, not the vocabulary.** A function that is a run of
    `set_field_object_*` calls in the original's order has renamed the addresses and kept
    the translation. Model the game's concepts above the accessors -- an object's animation
    as a state machine with named states, its movement, the story flags, the party -- as
    modules whose operations are named in game terms, and have ports call those. The per-byte
    writes the byte-exact gate still needs live inside the operations, one place per concept,
    which is also where the storage moves when the gate retires. An in-band encoding the
    original uses (`$FF` for "none") becomes a named state inside the implementation.
  - **An interpreter dispatches through a C table**, never through the ROM's pointer table
    and an emulated `JSR`. Opcodes that differ only in how their operands are encoded (a short
    form, a constant against an operand, 8- and 16-bit) are one operation and several table
    rows, and the table decodes operands into typed, named parameters, so a handler reads
    `play_animation(g, obj, animation, frames)`, not `operand(g, c, 1)`. Inlined handlers
    keep their time with `NATIVE_TIME_AS` and stay registered, so each can be fuzzed alone.
  - **Comments say what a function is for, in the game's terms**: what it does to the game,
    when and why it happens, what it changes. Not the function's name again, not the opcode's
    encoding, not the original's mechanics. The mechanics belong in a comment only where the
    port deliberately reproduces them, and then the comment says why. A file's header says
    what part of the game it is and how its pieces fit together.
  - **ROM data is loaded, not addressed.** Reads of ROM go through a named accessor for the
    resource (`sound_data.h`, `scene_data.h`, `screen_data.h`, `field_data.h`,
    `palette_data.h`), never `m_read8(s, 0xD15E10 + i)` in the middle of a routine. Where
    MD2 has typed the resource (docs/MATCHING.md) the accessor takes that type's name. This
    is what lets a later build load the same asset from an extracted file or a converted
    format without revisiting the port.
  - Write what the routine *does*, not what the draft says. Don't carry over the draft's
    shape: emulated register variables, dead flag updates, block boundaries, or a name from
    its address. Keep the `// $BB:AAAA` comment and nothing else mechanical.
  - Reach work RAM through the accessors in `game/vars.h` (`battle_menu_member(g)`, not
    `0x95D5` and not `RAM_BATTLE_MENU_MEMBER`), generated from `re/ram.toml`; `game/ram.h`
    is the older, address-shaped form, for generated code and the ports not converted yet.
    Add what the port learns to the map and regenerate both (docs/DISASSEMBLY.md, "The RAM map").
  - Reach the host only through `runtime/platform/` (a lint test enforces it). No PPU, DMA,
    HDMA or APU register number appears in `game/`. Read and write the machine through
    `machine.h`'s `m_read8`/`m_write8` and friends, never by indexing a mapped image, so a
    build that loads its data from somewhere else can redirect it.
  - Keep SNES *semantics* -- tiles, palettes, priority, window logic -- and drop SNES
    *mechanics*: which register latches what, channel programming, write order, the timing
    of a write. The services exist to express the first and hide the second.
  - Don't bake in what a later target changes: the 256x224 screen, the 0-255 coordinate
    range, a tile's size or bit depth, or a fixed bank layout. Take them from the service or
    give them a named constant. Screen coordinates are signed, so positions off the original
    screen survive widescreen.
  - Where the original waits on beam position or depends on exact timing, don't reproduce
    the wait silently: port what it computes, say so in a comment at its `// $BB:AAAA`, and
    add it to docs/TODO.md. Those routines are what block the platform milestones.
  - Keep the logic compilable for the 65816 as well as the host (docs/TODO.md, "A SNES ROM
    compiled from the hand ports"). Put what the routine does in a C function with real
    parameters and results, and keep the translation to and from the original's registers,
    flags and direct page in a thin wrapper at its entry: that wrapper is what a SNES build
    replaces with glue to the original calling convention, and the logic compiles for either.
    Use 8- and 16-bit types where the value fits; no floating point, 64-bit integers,
    recursion or large locals; no C library beyond `memcpy`, `memset` and `memcmp`.
    `tests/test_port_subset.py` enforces the mechanical part.
  - A port that can't be expressed through a service yet is a finding, not a defeat: add the
    call the subsystem needs (additive), or use the documented SNES escape and write down
    why, as docs/PLATFORM.md's "as built" sections do.
  - Ports don't carry cycle costs: no per-path timing numbers in `game/`. A port's time is
    its `cost_milli` estimate, and the timing gate measures and bounds the estimates' error
    (docs/RECOMP.md, "The timing gate"). A port that runs another routine's work inline (a
    dispatcher's handlers) names that routine with `NATIVE_TIME_AS(s, 0xBBAAAA, ...)`, so the
    work is estimated at that routine's own calibrated value: a tag, never a number
    (docs/PORTING.md, "Writing a port"). Fuzzing is part of a port's definition of done,
    and a shadow run's `interrupted` count is not noise: an interrupted call is one shadow
    mode did **not** compare, so a new entry with a non-zero count has paths nothing has
    checked. Fuzz those deliberately (two of the Epoch screens' five fuzz-found bugs were on
    such paths; docs/RECOMP.md, "Phase 3, wave 2").
  - Declare what the original leaves dead rather than reproduce it: flags nobody reads,
    either byte of A (`DC_AH`, `DC_AL`), a subroutine's stack frame. An entry's don't-care
    set, `m_stop_at` where a port stops (`m_stop_at_own` where the stop's set is its own,
    not the entry's too), and the don't-care sets of call-outs say so, and shadow mode doesn't compare
    them (docs/PORTING.md). A declaration is a claim about the original, so it must be one the
    liveness analysis agrees with: `tools/dontcare_check.py` checks every one, and the gate runs
    it. A call-out's set must hold even where the callee's targets are picked at run time (a
    dispatcher reading RAM); where only fuzzing can show that, the declaration carries a
    `dc-evidence:` note naming a `ct_fuzz --poison-dc` run (docs/PORTING.md, "Declarations
    liveness can't prove"). A port stops with `m_stop_at` and its address, and writes the addresses and sets as
    constants, or from a helper's parameters, locals or a table in the same file, so the check
    can read them.
  - Still write the scratch cells the original writes, even ones that look like temporaries:
    other code may read them, and shadow mode compares memory. Find the reader first. What
    only shadow mode or still-generated code reads is scaffolding: write its final value once
    through `ORIGINAL_EFFECT(...)` (`game/original_effects.h`, docs/PORTING.md "Scaffolding"),
    which the native-shape build compiles out, and never use the cell as working state.
  - Keep the original's index arithmetic, wraparound and odd offsets included, rather than
    tidying it: fuzzing reaches the inputs where it differs, and so can the game.
- **Writing host-side code** (docs/PLAYING.md, "How the player is built"). `game/` is kept
  portable by the platform services; what a host that isn't a desktop runs into instead is
  the frontend, so hold the line there too. A host that calls back once a frame, with no
  filesystem and no stack of its own, is the case these are for:
  - The loop belongs to the driver (`frontend/window.c`, `frontend/headless.c`).
    Below it nothing sleeps, spins on a clock, polls an event queue or blocks on a device:
    the game advances one frame per call, and input arrives through `in_host_set`.
  - Persistence moves bytes under a name, not a path. A new runtime or player function takes
    a buffer; opening a file, choosing a directory and knowing what a save is called are the
    driver's, which is also what makes a save survive a host with no `fopen`.
  - Stack switching goes through `runtime/port/fiber.h`, which is where a host that can't
    switch stacks natively gets its one implementation. No `ucontext`, thread or `setjmp`
    stack trick anywhere else.
  - Keys, pads and window geometry are bindings in a driver, not player state. A second
    frontend should be able to differ in all three without touching the player.
- Keep the repo machine-independent: no local paths, drive letters, or
  toolchain install locations in code, docs, or build files.
- Never commit ROM bytes or anything derived from them: extracted assets, SPC
  dumps, traces, disassembly listings with data bytes, save states.
  Cartridge saves (`.srm` battery saves: game progress, not assets) are fine and
  live in `oracle/saves/`; emulator save states are not.
- Reference emulator cores are supplied locally under `cores/` and never
  vendored.
- Every ported routine carries its original address in a `// $BB:AAAA` comment.
- Keep the docs current as work lands, in the same commit as the work: the
  milestone docs (docs/APPROACH.md, RECOMP.md, MATCHING.md, PLATFORM.md,
  RUNTIME.md, PORTING.md) say what was built and how it was measured,
  docs/TODO.md holds the open follow-ups, and docs/PLAN.md says what to do next
  and why the rest waits.
- **Delete finished work from the plan and the follow-ups rather than marking it
  done.** A completed item leaves docs/PLAN.md and docs/TODO.md entirely; what
  it produced belongs in its milestone doc's "as built" section, which is where
  the history lives. No "(done)" annotations, no struck-through lines, no
  completed-work sections.

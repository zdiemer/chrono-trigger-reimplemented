# chrono-trigger-reimplemented

Native C reimplementation of Chrono Trigger (SNES, USA), validated by
native movies, recorded outcomes and routes. Strategy: docs/APPROACH.md.

## Facts

- ROM: No-Intro *Chrono Trigger (USA)*, CRC32 `2D206BF7`. HiROM+FastROM, 4 MiB,
  8 KiB SRAM, no coprocessor. `tools/rom.py stage <path>` verifies it and
  copies it to `roms/ct-usa.sfc` (gitignored).
- Addresses are written in SNES bus form, `$C0:1234`. HiROM file offset =
  `(bank & 0x3F) << 16 | addr` for banks `$C0-$FF` (and `$40-$7D`).

## Commands

- Stage a verified copy of the USA cartridge: `python3 tools/rom.py stage <path>`.
- Configure: `cmake -S . -B build/main -G Ninja -DCMAKE_BUILD_TYPE=Release -DCT_WERROR=ON`.
  Build the game and its tests: `cmake --build build/main -j 2`. No ROM is needed to build.
  SDL2 enables the window and audio driver; otherwise the executable is headless.
- Play: `build/main/ct_native --rom roms/ct-usa.sfc`. See docs/PLAYING.md.
- Validation: `python3 tools/suite.py --ct-native build/main/ct_native -j 2` runs
  sampled native movies to completion, dark-screen checks, recorded save/place
  ratchets, three native picture fingerprints, fast native routes, snapshot continuations
  (`tools/player_check.py`), portstyle, portsmell, doc-names, and Python/native unit tests.
  Only the last line `suite OK` passes. The final `times:` and `wall time:` lines
  include unit tests; the gate fails at 600 seconds. Use `.exe` on Windows.
  `--movies <name>...` narrows iteration without pruning unseen outcome rows.
- Non-blocking batch: `python3 tools/suite.py --ct-native build/main/ct_native -j 2 --long`
  runs the whole movie library and every native route, outside the gate budget.
  Logs and `findings.json` go in `build/suite-long` (or `--out`). Findings become
  fixes; this batch never blocks a commit, merge or push and is not required CI.
  It reports findings with exit status 0, never `suite OK`; setup errors still fail.
- ROM-free tests: `python3 -m unittest discover tests` and
  `ctest --test-dir build/main --output-on-failure --no-tests=error -j 2`.
  CI builds ct_native and these tests with warnings as errors on Linux gcc/clang,
  macOS clang and Windows MinGW gcc.
- Routes: `python3 -m tools.movies.route_check --drive build/main/ct_native`
  runs New Game+ and the inventory item-use route. Pass route names for manual
  batches; `--scratch <dir> --resume` retains checkpoints. The Python driver
  starts `ct_native --drive` over stdin/stdout. See docs/MOVIES.md.
- Source checks: `python3 tools/portstyle.py --check` and
  `python3 tools/portsmell.py --check`. Read their findings before qualitative
  review; neither can establish that a port reads as a hand reimplementation.
  `python3 tools/doc_names.py --check` fails where docs/*.md, CLAUDE.md, a
  skill or a `source`/`meaning` string in re/ram.toml names a function, constant
  or file the code no longer has: rename or delete the mention in the branch that
  removes the name (deliberate mentions: `tools/doc_names_exceptions.txt`).
- Source inventories: `python3 tools/machine_joints.py`,
  `python3 tools/state_inventory.py`, and `python3 tools/ram_map.py storage`.
  `ram_map.py accessors --check` checks generated state accessors;
  `tools/assets.py check` checks the authored resource catalogue.
- `tools/dist.py` builds and packages ct_native; `tools/package_native.py` packages
  an existing build for the four-platform native-player CI artifacts. Neither
  includes cartridge data. `ct_native --check-snapshots EVERY[,SPAN]` plays every
  snapshot's frames twice and fails where they differ (docs/PLAYING.md, "Snapshots").

## Rules

- **Writing game code** (docs/PLAN.md, docs/PORTING.md). The port is source, and the later
  goals (a native renderer, widescreen, a GBA or browser build, assets as data, a SNES ROM
  compiled from the ports themselves) are made
  easy or hard here rather than in a backend:
  - **This is a reimplementation, not a transliteration, so the code is expected to
    diverge from the original's shape.** What must match is observable behaviour, which
    the gate measures; nothing requires the port to reach it the way the ROM does. A port
    that still moves an emulated A, X and P about, spells its variables as addresses and
    follows the original's basic blocks has translated the assembly rather than
    reimplemented the routine, however well it passes. Judge a finished port by whether
    someone who has never seen the ROM can read it and say what the game does. Keep the
    `// $BB:AAAA` comment and nothing else mechanical.
  - **Operations on game state.** Functions take `GameState *`, the `Platform *`
    services they need and plain values, and return C results. Don't recreate
    registers, flags, a stack or entry wrappers. See docs/PORTING.md for callbacks,
    mode hand-offs and work accounting.
  - **Build a vocabulary and call it.** The routine's work belongs in named functions --
    `draw_window_frame`, `load_slot_summary`, `hide_sprites` -- not in a run of
    accessor calls and bit operations the reader has to simulate. If a function is a long
    straight line of them, the abstraction it needed hasn't been written yet: write it, in
    the file if it is local, in the subsystem's header if shared, in `runtime/platform/` if
    it is the host.
  - **The accessors are the storage, not the vocabulary.** A function that is a run of
    `set_field_object_*` calls in the original's order has renamed the addresses and kept
    the translation. Model the game's concepts above the accessors -- an object's animation
    as a state machine with named states, its movement, the story flags, the party -- as
    modules whose operations are named in game terms, and have ports call those. Storage
    details live inside the operations, one place per concept. An in-band encoding the
    original uses (`$FF` for "none") becomes a named state inside the implementation.
  - **An interpreter dispatches through a C table**, never through the ROM's pointer table.
    Opcodes that differ only in how their operands are encoded (a short
    form, a constant against an operand, 8- and 16-bit) are one operation and several table
    rows, and the table decodes operands into typed, named parameters, so a handler reads
    `play_animation(g, obj, animation, frames)`, not `operand(g, c, 1)`. Inlined handlers
    keep their existing time with `TIMED_AS` and have native tests for their behavior.
  - **Comments say what a function is for, in the game's terms**: what it does to the game,
    when and why it happens, what it changes. Not the function's name again, not the opcode's
    encoding, not the original's mechanics. The mechanics belong in a comment only where the
    port deliberately reproduces them, and then the comment says why. A file's header says
    what part of the game it is and how its pieces fit together.
  - **ROM data is loaded, not addressed.** Reads of ROM go through a named accessor for the
    resource (`sound_data.h`, `scene_data.h`, `screen_data.h`, `field_data.h`,
    `palette_data.h`), never a cartridge address in the middle of a routine. Where
    the resource catalogue declares a type, the accessor takes that type's name. This
    is what lets a later build load the same asset from an extracted file or a converted
    format without revisiting the port.
  - Reach work RAM through the accessors in `game/vars.h` (`battle_menu_member(g)`, not
    `0x95D5` and not `RAM_BATTLE_MENU_MEMBER`), generated from `re/ram.toml`; `game/ram.h`
    is the older, address-shaped form that unconverted code still uses.
    Add what the port learns to the map and regenerate both (docs/STATE.md).
  - Reach the host only through `runtime/platform/` (a lint test enforces it). No PPU, DMA,
    HDMA or APU register number appears in `game/`. Use typed state accessors, named
    resources and platform services, never a mapped memory image.
  - Keep SNES *semantics* -- tiles, palettes, priority, window logic -- and drop SNES
    *mechanics*: which register latches what, channel programming, write order, the timing
    of a write. The services exist to express the first and hide the second.
  - Don't bake in what a later target changes: the 256x224 screen, the 0-255 coordinate
    range, a tile's size or bit depth, or a fixed bank layout. Take them from the service or
    give them a named constant. Screen coordinates are signed, so positions off the original
    screen survive widescreen.
  - `game/` is the reimplementation alone (docs/PLAN.md, "Layering"). Backends, asset
    decoders, frontends and enhancements live outside it and are never included from it.
    An enhancement that changes what is shown reaches `game/` as a service query or a named
    option whose default is the original. It never changes the game's rules, and the gate
    runs at the defaults.
  - Where the original waits on beam position or depends on exact timing, don't reproduce
    the wait silently: port what it computes, say so in a comment at its `// $BB:AAAA`, and
    add it to docs/TODO.md. Those routines are what block the platform milestones.
  - Keep the logic suitable for the 65816 as well as the host (docs/TODO.md, "A SNES ROM
    compiled from the hand ports"). Put what the routine does in a C function with real
    parameters and results. Target-specific glue belongs outside the game logic.
    Use 8- and 16-bit types where the value fits; no floating point, 64-bit integers,
    recursion or large locals; no C library beyond `memcpy`, `memset` and `memcmp`.
    `tests/test_port_subset.py` enforces the mechanical part.
  - A port that can't be expressed through a service yet is a finding, not a defeat: add the
    call the subsystem needs (additive), or use the documented SNES escape and write down
    why, as docs/PLATFORM.md's "as built" sections do.
  - Ports don't carry per-path cycle numbers. Preserve the existing work rates in
    `runtime/port/time_rates.c`; changes need native gameplay evidence. Keep
    `TIMED_AS` tags where inlined work still uses its own rate.
  - Keep working state in C values. Comparison-only scratch writes retired with
    the byte-exact gate; do not reintroduce them (docs/PORTING.md, "Scaffolding").
  - Keep the original's index arithmetic, wraparound and odd offsets included, rather than
    tidying it: the game can reach the inputs where it differs.
- **Writing host-side code** (docs/PLAYING.md, "How the player is built"). `game/` is kept
  portable by the platform services; what a host that isn't a desktop runs into instead is
  the frontend, so hold the line there too. A host that calls back once a frame, with no
  filesystem and no stack of its own, is the case these are for:
  - The loop belongs to the driver (`frontend/window.c`, `frontend/ct_native.c`).
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
  live in `saves/`; emulator save states are not.
- Every ported routine carries its original address in a `// $BB:AAAA` comment; code ported
  from the sound program (`game/sound/`) uses `// SPC $AAAA`, with every address where one
  function stands for several routines.
- Keep the docs current as work lands, in the same commit as the work: the
  milestone docs (docs/APPROACH.md, REIMPLEMENTATION.md, STATE.md,
  PLATFORM.md, PORTING.md) say what was built and how it was measured,
  docs/TODO.md holds the open follow-ups, and docs/PLAN.md says what to do next
  and why the rest waits.
- **A milestone doc is a document, not a log.** It holds the mechanisms, the
  measurements and one worked example of each kind of work; the run of per-chunk
  accounts behind them goes to docs/history/, and a ledger (a row per port, per
  entry, per exception) goes to a file made to be `grep`ed -- `tools/*_baseline.txt`, `tools/*_exceptions.txt`. Both are written for a reader
  who arrives with no context, and every agent pays to read them again, so what
  earns its place there is what is still true and still needed.
- **Delete finished work from the plan and the follow-ups rather than marking it
  done.** A completed item leaves docs/PLAN.md and docs/TODO.md entirely; what
  it produced belongs in its milestone doc's "as built" section, which is where
  the history lives. No "(done)" annotations, no struck-through lines, no
  completed-work sections.

## Working in this repo as an agent

Several agents often share one machine and one repository, each in its own
worktree under `.claude/worktrees/`. `AGENTS.md` is this file.

- **Processes.** Build with `-j 2` and run at most two `ct_native`
  processes at a time unless told otherwise. Kill a process only by its PID, never
  by image name (`taskkill /IM`, `pkill ct_native`): that kills other agents' runs.
- **The gate.** Run `py tools/suite.py --ct-native build/main/ct_native.exe -j 2`.
  Read its last line: only `suite OK` passes, within the 600-second budget including
  unit tests. Run longer routes by hand only when a change concerns that route.
- **Ratchets and exceptions.** Never add a line to a baseline or exceptions file
  (`tools/*_baseline.txt`, `tools/*_exceptions.txt`)
  to get a check to pass without a reason on the line that a reviewer would
  accept; fix the cause where it can be fixed. Never claim a fix the diff
  doesn't contain.
- **Calling another operation.** Preserve the callee's real game-state
  requirements, work charges and observable order.
- **Names.** No `_as_c`, `_now` or similar suffix: the C function takes the plain
  name. A sequence two callers share is `*_with`.
- **Commits.** Plain messages with no `Co-Authored-By`, session or other trailers.
  Commit each coherent step on your branch so work survives an interruption.
  Don't push or merge unless asked.
- **Worktrees.** Before `git worktree remove --force`, check the worktree for links
  into the main checkout (on Windows,
  `Get-ChildItem -Recurse -Force -Attributes ReparsePoint <worktree>`): removing
  one deletes what it points at.

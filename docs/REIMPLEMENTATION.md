# Native reimplementation

ct_native is the product. The last commit containing the emulator and oracle tooling is tagged `emulator-oracle`.

## The lean gate

`tools/suite.py --ct-native build/main/ct_native -j 2` runs the sample in
`tools/gate_movies.txt`, the New Game+ and inventory item-use routes, portstyle,
portsmell, Python unit tests and the native CTest tests. Every sampled movie
must reach its declared frame count, exit successfully and avoid an unexplained
dark ending. `suite OK` is the final pass line; wall time includes every stage
and must be below 600 seconds. The separate `--long` batch runs the whole library and every public route
module at `-j 2`, with stage logs and `findings.json` under `build/suite-long`.
Gameplay failures remain findings, but a missing binary or a stage launch
failure aborts the batch with nonzero status and a diagnostic. It reports
findings without a gate time budget; fixes follow from those findings, and
commits, merges and pushes never wait for the batch.

Recorded outcomes protect the selected saved-game bytes, cartridge save RAM
and ordered place visits. The ledger records the movie hash and projection
hash so a stale artifact or changed projection cannot silently pass. Missing
movies, incomplete states and runtime diagnostics fail. Existing native
baseline matches remain protected; a pinned exception accepts only the recorded
difference, never a new state or missing visit. Subsets do not prune other rows.
Pictures are checked for dark endings. Three native fingerprints in
`tools/native_picture_baseline.txt` protect the accepted title BG1 clear in
attract-demo and both Golem Twins movies. They pin native frame hashes after
the fill; no emulator is built or run. Main's accepted values and reasons are
retained, independently of the save/place ratchets.

The repeated whole-tree smell scans in the Python tests share one immutable
result per test process; every individual smell fixture still executes. Native
C tests cover the frame scheduler, video, tasks, timing, cartridge save round
trips, asset loading, route protocol, audio replay, persistence and bindings.

## Validation receipt

After integrating the native player, owner/alias storage and scene fills, the
sampled native gate finished `suite OK` in **309.6 seconds** at `-j 2`:
201 Python tests (87.8 s), 15 native C tests (1.2 s), 58 movies (145.7 s),
58 outcome ratchets (0.2 s), both fast routes (0.8 s), portstyle (0.2 s) and
portsmell (73.9 s). All three native picture fingerprints passed. No native
save/place baseline value or exception changed.

Windows MinGW GCC and WSL GCC/Clang built with warnings as errors; both Linux
builds passed all 15 C tests. Opening, menu and battle quick-load continuations
matched uninterrupted state, pictures and PCM; route slot/file restores and
native recording playback passed too. The packaged Windows player passed its
launch smoke check. The CI matrix also configures macOS Clang; that platform
was not run locally.

## Game and host

`game/core/program.c` boots the game and dispatches its frame work. The game
uses platform services for video, audio, input, saves and timing. The desktop
frontend owns the window, event polling and files. Route tools drive the same
program over stdin/stdout with `--drive`.

Game operations take `GameState *g` and, only when needed, `Platform *pf`, plus
named parameters and C results. Separate parameters keep state-only calculations
independent of services and service-only uploads independent of game storage.
Neither handle contains an emulated CPU. The frontend owns the frame loop and
files; the timing service owns coroutine scheduling.

Core stores typed `ProgramHandler` callbacks in per-game state. Modes install
their vblank and line functions directly. Frame dispatch reads those pointers;
it never decodes a jump instruction or searches a table by address. Scene, warp,
field restart, title and reset hand-offs use normal calls and named requests.
Invalid state records the first diagnostic and returns; there is no fallback
to cartridge code. The program test covers callback isolation, replacement,
disabling, mode hand-offs and failed initial scenes without a ROM.

The former machine entry graph had no native callers. Deleting it across all
subsystems removed the registry, held-continuation ledger, game time-tag rows,
console vectors, entry macros, machine access helpers, CPU shell headers and
`no_machine.c`. Native C tables retain their real operations and timing keys.
Machine-only cost APIs and their dead calculations were removed too. These
deletions do not finish the vocabulary and storage work in every subsystem:
single-implementation adapters and some register-shaped results still need
simplification. The video escapes have named services (PLATFORM.md, "Memory
moves and VRAM read-backs"); the sound driver's register write is the one
escape left.

### Time without a machine

`TIMED_AS(g, key, operation)` selects an authored work rate. Accessors, resource
reads and explicit charges accumulate native work; the scheduler converts that
debt into lag frames, retaining fractional carry. Rates remain unchanged in
`runtime/port/time_rates.c`; an address-shaped key identifies an estimate, never
a function to execute. There is no generator or emulator calibration at build
time. Untimed validation remains excluded where it was excluded before.

Removing a read can change the estimate even if its value is dead. Callback
selection therefore retains four access units, and disabling both callbacks
retains sixteen. A battle upload guard used to inspect both request flags
seventeen times, plus five bytes for a queued transfer and two for a block;
the stack guard is gone, but its counted work remains an explicit charge.
The lean gate caught the missing charge through battle picture and outcome
changes; restoring the work restored those checks without changing baselines.

### Warp as the subsystem pattern

Warp is the pilot because its small mode includes setup, animation, frame
waits, line effects and a return to field. `warp_runs(g, pf, scene)` selects a
`WarpKind`, installs typed callbacks, preserves the story flags, and builds
frames until the trip ends. `make_a_frame` updates the plane, light and circle
and returns whether another frame is needed. The timing service yields while
the frame is pending; completion restores the story flags and returns to the
program loop. No wrapper, address stop, emulated return or register escape
remains in core or warp. PORTING.md gives the steps for other subsystems.

### Source inventory

`tools/machine_joints.py --files` measures source dependencies, not coverage.

| Joint | Before | After |
| --- | ---: | ---: |
| Functions taking `Snes *` | 2009 | 0 |
| Registered entries / held continuations | 13 / 136 | 0 / 0 |
| Emulated returns / address stops | 505 / 111 | 0 / 0 |
| Address call-outs / sibling lookups | 14 / 9 | 0 / 0 |
| Machine time tags | 8 | 0 |
| Register escapes | 20 | 1 |
| Video service calls | 543 | 551 |
| Missing video backends / cartridge reads by address / scaffolding | 0 / 0 / 0 | 0 / 0 / 0 |

## Scaffolding removal

Comparison-only writes and scaffold-read instrumentation were removed. The
source inventory's scaffolding count fell from 13 to zero. All 58 sampled native
outcome artifacts were byte-identical before and after that deletion, including
the saved-game and place projections. No game logic or timing estimate changed.

## State and resources

Typed accessors describe the game's state; STATE.md records the struct storage
and compatibility projection. Cartridge `.srm` saves still load through the
real save-slot routines. Their round trip is tested without a ROM.

The authored asset catalogue in `re/assets.tsv` generates the committed C
lookup table with `tools/assets.py generate`. The native frontend imports a
verified user cartridge into a local pack and then serves resources by name.
The pack and all captured runtime state remain uncommitted.

## Native route findings

Son of Sun's approach now corrects the settled flight position before landing,
so it no longer relies on a fixed coast reaching the desired tile. Neither
Son of Sun nor Epoch Lavos has a route exception. Native continuations reach
Fiona's location 424, Sun Stone's rewards, Dalton's outcome and Mammon Machine
after the first Queen Zeal fight. These distinguish missed fixed-movie inputs
from a timing-model defect; they do not justify changing work rates. Keep the
Sun Stone and Queen Zeal continuations reproducible as route checks (TODO.md).

All 2,236 live variables now have module-owned storage, and 1,511 of them
typed storage, counting a value as typed only when it is exactly one owner field
or element of its own width; 725 remain blob-backed, nearly all overlays where
one mode reads another mode's fields at other widths, and none is image-backed. The 27 retired
sound-loader variables and the residual 128 KiB work-RAM image were removed.
The baseline was 1,953 typed, 283 blob-backed and 27 image-backed variables.
The generated address view supports cartridge saves, route inspection and
remaining dynamic/bulk operations. The generator also emits designed field-object, combatant, character, menu
and sprite records without changing the public accessors.
[STATE.md](STATE.md) describes the ownership rules and validation.

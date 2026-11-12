# Native reimplementation

ct_native is the product. The last commit containing the emulator and oracle tooling is tagged `emulator-oracle`.

## The lean gate

`tools/suite.py --ct-native build/main/ct_native -j 2` runs the sample in
`tools/gate_movies.txt`, the New Game+ and inventory item-use routes, portstyle,
portsmell, Python unit tests and the native CTest tests. Every sampled movie
must reach its declared frame count, exit successfully and avoid an unexplained
dark ending. `suite OK` is the final pass line; wall time includes every stage
and must be below 600 seconds. Longer routes remain explicit manual/batch work.

Recorded outcomes protect the selected saved-game bytes, cartridge save RAM
and ordered place visits. The ledger records the movie hash and projection
hash so a stale artifact or changed projection cannot silently pass. Missing
movies, incomplete states and runtime diagnostics fail. Existing native
baseline matches remain protected; a pinned exception accepts only the recorded
difference, never a new state or missing visit. Subsets do not prune other rows.
Pictures are checked for dark endings, not against emulator frame hashes.

The repeated whole-tree smell scans in the Python tests share one immutable
result per test process; every individual smell fixture still executes. Native
C tests cover the frame scheduler, video, tasks, timing, cartridge save round
trips, asset loading, route protocol, audio replay, persistence and bindings.

## Validation receipt

The sampled native gate finished `suite OK` in **337.4 seconds** at `-j 2`:
192 Python tests (85.1 s), 12 native C tests (1.4 s), 58 movies (171.7 s),
58 outcome ratchets (0.2 s), both fast routes (0.6 s), portstyle (0.2 s) and
portsmell (78.0 s). No native save/place baseline value or exception changed.
Windows MinGW GCC and WSL GCC/Clang built with warnings as errors; both Linux
builds passed all 12 C tests, and Linux Python passed all 192 tests. The CI
matrix also configures macOS Clang; that platform was not run locally.

## Game and host

`game/core/program.c` boots the game and dispatches its frame work. The game
uses platform services for video, audio, input, saves and timing. The desktop
frontend owns the window, event polling and files. Route tools drive the same
program over stdin/stdout with `--drive`.

The build links native services and state storage only. Existing entry wrappers,
registry source and held-wait descriptions remain for the machine-removal work;
the registry is not a build input. The native host rejects a reached machine-only
operation. Existing authored work rates are frozen in `runtime/port/time_rates.c`
with their values unchanged; there is no build-time generator or calibration.

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

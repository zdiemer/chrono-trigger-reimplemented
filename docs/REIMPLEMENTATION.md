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

## Native route findings

Son of Sun's approach now corrects the settled flight position before landing,
so it no longer relies on a fixed coast reaching the desired tile. Neither
Son of Sun nor Epoch Lavos has a route exception. Native continuations reach
Fiona's location 424, Sun Stone's rewards, Dalton's outcome and Mammon Machine
after the first Queen Zeal fight. These distinguish missed fixed-movie inputs
from a timing-model defect; they do not justify changing work rates. Keep the
Sun Stone and Queen Zeal continuations reproducible as route checks (TODO.md).

The typed-state review corrected the ownership-only measure: 1,953 variables
now have typed storage, 283 are blob-backed and 27 remain image-backed, out of
2,263. The honest baseline was 785 typed and 1,451 blob-backed. Composed views,
page-owner arrays and named scalar fields share one address projection; narrow
anonymous remnants remain explicit buffers. STATE.md gives module counts and
validation, including cartridge saves and route/video address queries.

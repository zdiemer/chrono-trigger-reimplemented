# Movies and routes

Movies in `movies/` are controller input in text `.ctm` format. Each header
names its frame count; save-starting movies identify a cartridge `.srm` by
CRC32. Fixtures live in `saves/`. Existing movie bytes are kept unchanged so
the recorded input hashes continue to identify the same inputs.

The lean gate selects `tools/gate_movies.txt`, with a reason for each movie.
It runs each to its end in ct_native and checks dark endings and recorded
saved-game/save-RAM/place outcomes. The sample does not establish that every
route reaches its intended outcome. Long library movies run separately.

## Routes as a test

Routes inspect game state and choose inputs, so they survive timing changes.
`tools/movies/driver.py` starts `ct_native --drive` and exchanges commands on
stdin/stdout. `--drive <executable>` or `CT_NATIVE` chooses the program;
`CT_NATIVE_ASSETS` can choose its local pack. There is no network endpoint.

```sh
python3 -m tools.movies.route_check --drive build/main/ct_native
python3 -m tools.movies.route_check beyond_time --drive build/main/ct_native --scratch build/routes --resume
```

The default fast set is `new_game_plus` and `menu_sweep:items`: the first checks
New Game+ and erasure, the second loads cartridge progress and uses an item.
A route whose ct_native printed a runtime fault (tools/outcomes.py's: an
overlay crossing, an accessor out of range) fails, as a movie does.

`playthrough` starts from a blank cartridge and plays the story on from a new
game. Its own phases cover what no save-based route plays (the opening, the
fair, 600 A.D.'s castle, shopping, the prison escape, the chase through
Guardia Forest to the Gate, Lab 16); where a save-based route already plays a
stretch, the route calls that route's `play(run, start)` from the state it
reached instead of a save (`_trial_cathedral`, `cronos_trial`, `dragon_tank`,
`guardian`). Its checkpoint names are unique across the pieces it composes. It
ends past the Guardian in Arris Dome, 2300 A.D., with the information
center's computer showing the Day of Lavos (frame 365902). Lab 16's Shadows,
which no weapon hurts, are run from by their enemy number; any other fight
with no enemy the bot can wear down fails the route.

The other legs of the playthrough start from cartridge saves so the whole
game is played in parallel. `playthrough_leg-d` starts from ctsave11 slot 1,
the party held in the Blackbird after Crono is gone, with the save's party
and items, and plays on through `play()` of `blackbird_save`, `dalton`,
`north_cape` (Magus joins; until he is met the Epoch shows Marle's resolve
instead of flying), the Black Omen's rising, Gaspar's Chrono Trigger, Gato's
silver points and Bekkler's clone (`fair_clone`; the clone is delivered to
Crono's room), Belthasar in the Keeper's Dome, Death Peak (`death_peak_wind`,
`death_peak`, `death_peak_return`; a Shelter before the last Spawn), Crono
joining in Lucca's place, and the bucket into Lavos (`other_endings`), whose
imitations of the earlier bosses it beats. It ends where the shell's own form
stands (frame 359416): at levels 34-38 the party takes 400-500 HP a member
from each of its blows and deals it some 40, so an ending waits on a party
grown by the Black Omen or by fighting. The
winged Epoch's boarding, dial, flight and landing are `routes/_epoch.py`;
`black_omen_entry` flies it into the Black Omen from ctsave14.
`python3 tools/suite.py --ct-native build/main/ct_native -j 2 --long` runs the
whole library, every public route module and the wide-view end-state check
(`tools/wide_check.py`, docs/PLATFORM.md) as a non-blocking batch. Findings
become fixes; the batch never blocks a commit, merge or push. Its stage logs,
route logs and `findings.json` live under `build/suite-long`. At most two native
processes run at once; individual routes retain their six-hour timeout.
A route must report its reached outcome and exit successfully. Timeouts stop
only that route's process tree. `--protocol-log` records requests and replies.

## Native program checkpoints

Route slots and state files are complete frame-end snapshots of the native
program (docs/PLAYING.md, "Snapshots"), so a restore costs the snapshot's size
whatever the frame. `--resume` keeps route checkpoints only when the route
scripts, binary and cartridge hashes agree.

A route can write its kept input as a movie. Unrestored probes and state files
without their `.pads` sidecar cannot silently become recorded input. Add short
regression movies for new failures and retain a concrete reason in the sample.

Player quick loads and route restores use the same snapshots
(`frontend/snapshot.c`). `--record` produces the same `.ctm` format, including
the starting cartridge-save checksum.

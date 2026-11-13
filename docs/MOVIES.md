# Movies and routes

Movies in `movies/` are controller input in text `.ctm` format. Each header
names its frame count; save-starting movies identify a cartridge `.srm` by
CRC32. Fixtures live in `saves/`. Existing movie bytes are kept unchanged so
the recorded input hashes continue to identify the same inputs.

The lean gate selects `tools/gate_movies.txt`, with a reason for each movie.
It runs each to its end in ct_native and checks dark endings and recorded
saved-game/save-RAM/place outcomes. The sample does not establish that every
route reaches its intended outcome. Long library movies run separately.

## Where a movie parts from the reference

A movie's places are compared with the reference play's, the original's own
timeline recorded in `tools/outcomes.txt`. A run that enters `$000` where the
reference enters a real place has had a game over: the party lost a fight,
the game went back to the title (`$1B1`), sometimes into the attract demo
(`$1F5`), and the movie's later presses happened to load a save (`$000` again,
then the save's place).

On 2026-11-12, 20 of the gate's 58 movies missed places. Each was played
beside the reference (the frozen `emulator-oracle` tag's `ct_dev --native off
--state-trace`) and compared, as that tag's drift tool compared strict runs, by
milestones (places, songs, fades, battle menus, text windows, story steps)
and saved-game states, with native work RAM logged each frame. No movie
parted where the two had done the same work in the same frames: every one
parts after native time has moved the game a few frames off the reference,
so the movie's fixed presses meet another turn, another position or another
window, and the play goes elsewhere from there. At that tag the same hand
ports, given the original's time, played 16 of the 20 as the reference to the
end; the other four (attract-demo, events-sun-stone, menus-epoch,
menus-shop-equip) its strict runs ended as the reference does, while its
ct_native, on the host time model, already missed the same number of places
(`tools/places_baseline.txt` is unchanged since; in events-sun-stone it left
the same map at the same frame, 10635). What moves the frames, measured:

- **Fights.** In nine fights measured, the combatants' turn timers start
  counting 8 to 24 frames sooner than in the reference (Golem Twins: 14, of
  which 7 are the setup's last frames before the battle's frame updates
  begin). The first command menus open early, the recorded commands meet
  other turns, and fights are lost: the four `$000` movies (Dalton, Giga
  Gaia, Golem Twins, Death Peak) and six others part in a fight.
- **Loads under heavy handler work.** A wait whose frame had heavy handler
  work (a load's sends in the vblank handler, up to two frames' worth) gives
  every lag frame after it only what that frame left the main thread
  (`main_threads_frame` in `runtime/platform/native/timing.c`), though the
  lag frames' own handlers do little. Such loads run long: a trip through a
  Gate lands 24 to 31 frames late (menus-epoch, menus-shop-equip,
  events-fionas-forest, events-cyrus), Ozzie's Fort's second floor fades in
  26 frames late. Giving each lag frame its own handlers' time brings 11
  more places on time over the gate, but moves other fights to other turns:
  the Black Tyrano and Son of Sun movies then lose theirs.
- **The overworld's half rate.** The world map runs a pass every two frames;
  a frame of drift puts native on the other phase, so a press lands a pass
  later and the Epoch lands 8 pixels further (menus-epoch, menus-shop-equip).
- **The field.** Objects that walk on their own reach other places by a
  frame or two of drift: one is 4/16 of a pixel off when the leader walks
  into it, and no window opens (battle-castle-flea, battle-castle-slash). In
  events-sun-stone the party, halted by an event, stops 4 pixels further on,
  so a later walk to the exit ends short of it and leaves 339 frames late.

Every place native enters is entered a frame early (two for the title), and
a press shows in native's state at the end of the frame it is made on, where
the reference's shows a frame later (the load screen's Start, a step). The
frame the driver runs starts at the vblank, so its end takes in the whole of
the main thread's pass that follows; the boot and the load screen are short
as well (issue #184).

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
ends in Arris Dome, 2300 A.D., with Doan's Bike Key (frame 434643): past the
Guardian to the information center, the room below's rat caught for the
password by waiting where its run passes, the sealed doors and the storeroom. Lab 16's Shadows,
which no weapon hurts, are run from by their enemy number; any other fight
with no enemy the bot can wear down fails the route.
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

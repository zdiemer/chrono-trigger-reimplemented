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

### Fights carried by routes

A recorded movie meets a fight with presses fixed to frames. A few frames of
native time decide which turn a press meets, so the fight's outcome, and every
place after it, follow the time model rather than the game. A route from the
same save chooses its commands by game state and wins (or, for the Lavos
game over, loses) whatever the frames. `tools/fight_routes.txt` names the gate
movies whose fight such a route carries, each movie's own route, the one its
header names: 16 movies, from Giga Gaia, the Golem Twins, the Black Tyrano,
Son of Sun, Magus at North Cape and the Lavos game over to Cyrus, Fiona's
forest and Death Peak's summit. The gate runs those routes beside the fast
pair (`routes` in the gate's `times:` line). For those movies the outcome check
(`tools/outcomes.py`) still holds every protected match in
`tools/outcomes_baseline.txt`, and `tools/places_baseline.txt` still counts
their places on time, but a difference elsewhere in their places, saved game
or save RAM needs no pinned exception, and may have none: the route is the
fight's outcome. Nothing else moved: every other movie's exceptions still pin
exactly what it observes.

A route wins by retrying a lost fight from its command menus, so a battle
that computed otherwise could still be won. A carried route therefore
reports, as it finishes, each fight's retries and the facts the fights leave
(`tools/movies/fight_facts.py`), and `route_check` fails it where a fight
took more retries than its line in `tools/fight_routes.txt` allows (none,
unless the line says why: Magus at North Cape changes his barrier element and
may take up to 24; main took 16), or where its levels, experience, gold,
items won or event memory differ from `tools/fight_facts.txt`. Those values
are each movie's reference end state, the frozen `emulator-oracle` tag's
development build playing the movie with the original's code, whose saved
game matches the movie's
`game` digest in `tools/outcomes.txt`. All 16 match exactly.

Dalton isn't carried. Its route wins, but the Golem Boss on the Blackbird's
wing leaves before it is beaten, so the party gets neither its 2,000 gold nor
its 2,500 experience (the reference has both, at frame 22087). Damage isn't
the difference: the first round takes the boss from 15,000 to 13,206 HP in
both plays. The boss leaves by frame 20259 with about 3,488 HP left (20981 with
2,268 when Robo punches instead of trying Uzzi Punch); the reference was
still hitting it at frame 21921, when it fell. The boss's countdown runs out
sooner in native (issue #320's battle timing, or the countdown itself), so
the movie keeps its pinned exceptions until that is found.

Measured with the time-model change of issue #321 (each lag frame gets its
own handlers' time) on a throwaway branch: all 17 fight routes then carried still win,
including Black Tyrano's and Son of Sun's, whose movies then lose their
fights, and the carried movies' outcome checks pass. What still fails there
is not carried: three native picture fingerprints (attract-demo and both
Golem Twins movies), events-jetbike-race-won's finish (31 frames early,
past the tolerance), events-ocean-palace-lavos ending in the original's own
dark scene, and the pinned exceptions of the fights whose routes are too
slow for the gate (Ozzie's Fort 35 s, the R Series 50 s, the Guardian 54 to
79 s, the Spekkio sweep 32 s, the Beyond Time and Bucket endings 52 to 97 s)
and of the other movies the change moves. Those routes run in the long
batch, as every public route does.

`playthrough` starts from a blank cartridge and plays the story on from a new
game. Its own phases cover what no save-based route plays (the opening, the
fair, 600 A.D.'s castle, shopping, the prison escape, the chase through
Guardia Forest to the Gate, Lab 16); where a save-based route already plays a
stretch, the route calls that route's `play(run, start)` from the state it
reached instead of a save (`_trial_cathedral`, `cronos_trial`, `dragon_tank`,
`guardian`, `proto_dome`, `r_series`). Its checkpoint names are unique across
the pieces it composes. In Arris Dome it catches the room below's rat for the
password by waiting where its run passes (a frame to spare, asserted;
measured in ct_native only, unconfirmed against the original), opens the sealed doors and trades the Seed for the Bike Key;
Lab 32's race is steered with `events-jetbike-race-won`'s input from the race's
first frame, as `proto_dome`'s own approach is; a rest in Proto Dome's Enertron
and Tonics before the R Series keep Crono and Lucca fit for it, the factory's
fights before it leaving their MP alone. It ends at the End of Time after the
R Series (frame 551350). Lab 16's Shadows,
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
`playthrough_leg-b` starts from fantasyanime ctsave05 slot 1 (the Forest
Maze, 65,000,000 B.C.) with the save's party and items: the maze and the
Reptite Lair (`nizbel`'s piece), the Mystic Mountains' Gate, Melchior forging
the Masamune in 1000 A.D., Medina's and Truce Canyon's Gates, Mid Tonics from
Dorino (the save has none), the Cursed Woods, where Frog joins, the Magic
Cave (`magic_cave`'s piece) and Magus's castle: the castle's approach,
`castle_slash`'s and `castle_flea`'s pieces, the hall's trapdoor, Ozzie's
corridor and the inner rooms (a search that plays ladders and stairs through)
to Magus's door (frame about 387000). Ozzie waits there behind a barrier the
party's blows barely touch; the leg stops before him, its MP spent.
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

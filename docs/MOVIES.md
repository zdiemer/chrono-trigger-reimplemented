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
A route writes only to its scratch directory (`--prefix`): one that changes a
file under `saves/`, or leaves git's account of the working tree otherwise
changed, fails (`tree_state`), so a long batch can't replace the saves the
movies start from.

### Fights carried by routes

A recorded movie meets a fight with presses fixed to frames. A few frames of
native time decide which turn a press meets, so the fight's outcome, and every
place after it, follow the time model rather than the game. A route from the
same save chooses its commands by game state and wins (or, for the Lavos
game over, loses) whatever the frames. `tools/fight_routes.txt` names the gate
movies whose fight such a route carries, each movie's own route, the one its
header names (or, for a hand recording, the route that names the movie): 19
movies, from Dalton, Giga Gaia, the Golem Twins, the Black Tyrano, Son of
Sun, Magus at North Cape and the Lavos game over to Cyrus, Fiona's forest,
Death Peak's summit, the unequipped armor's fights and the TerraMutant. Six
routes take the movie's own presses for the field before their fights, which
the field plays on time: `unequipped_armor` to Truce Canyon (16 s where the
world map's searches took 41), `terramutant` to the boss, and `black_tyrano`,
`giga_gaia`, `rust_tyrano` and `north_cape` (the fight) to their battles' first
command menus, where walking there took 40-80% of each route's time (black_tyrano
23 s to 7, north_cape 23 to 4). Their `play` functions still walk the way for the
playthrough legs and `magus_joins`; from the movie's start, Magus's fight is
checked with seed 2, the first that wins without the party falling. The TerraMutant's
movie ends partway through the fight, so its route stops there too, the head
hurt as far as the movie's, both parts up. The gate runs those routes beside the fast
pair (`routes` in the gate's `times:` line). For those movies the outcome check
(`tools/outcomes.py`) still holds every protected match in
`tools/outcomes_baseline.txt`, and `tools/places_baseline.txt` still counts
their places on time, but a difference elsewhere in their places, saved game
or save RAM needs no pinned exception, and may have none: the route is the
fight's outcome. A protected saved-game or save-RAM end still holds; the
TerraMutant's, which its movie no longer reaches, is lowered on its baseline
line with the reason. A picture fingerprint of such a movie (`tools/native_picture_baseline.txt`,
`until=FRAME`) keeps to the pictures before its fight. Nothing else moved:
every other movie's exceptions still pin exactly what it observes.

A route wins by retrying a lost fight from its command menus, so a battle
that computed otherwise could still be won. A carried route therefore
reports, as it finishes, each fight's retries and the facts the fights leave
(`tools/movies/fight_facts.py`), and `route_check` fails it where a fight
took more retries than its line in `tools/fight_routes.txt` allows (none,
unless the line says why: Magus at North Cape changes his barrier element and
may take up to 24; main took 16), or where its levels, experience, gold,
inventory or event memory differ from `tools/fight_facts.txt`, or where the
file has no facts for it. The inventory is held both ways, every item but the
consumables, Tonic to Lapis, whose use goes with a fight's course. Those values
are each movie's reference end state, the frozen `emulator-oracle` tag's
development build playing the movie with the original's code, whose saved
game matches the movie's
`game` digest in `tools/outcomes.txt`. All 17 match exactly.

Dalton's Golem Boss counts down once its turns pass a point and leaves when
the count runs out, without its 2,000 gold and 2,500 experience. Played beside
the reference, native's count went up on the same turn (the 286th tick of its
turn timer) and every blow did the same damage; what differed was the route's
play. It asked Robo for tech 5, Uzzi Punch, which this save's Robo hasn't
learned: his list's entry 5 is a double tech past the blank row, which the
menu refused or held for its partner, so his turns went by without a blow
and the boss left first. The route now takes the single techs only
(`policies.single_tech_known`), Robo Tackle beats the boss, and Dalton is
carried again.

The battle's time calibrated against the original's (issues #320 and #321,
REIMPLEMENTATION.md) moved every fight's turns: all 19 carried routes still win
with their facts, and the gate's 58 movies keep 394 places on time (383 before)
and 476 in the reference's order (470). The fights whose routes are too slow
for the gate (Ozzie's Fort 35 s, the R Series 50 s, the Guardian 54 to 79 s,
the Spekkio sweep 32 s, the Beyond Time and Bucket endings 52 to 97 s) keep
their movies' pinned exceptions, re-recorded with the places they keep; their
routes run in the long batch, as every public route does.

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
fights before it leaving their MP alone. At the End of Time Spekkio's three
laps pass each post once (a post counts again each time its count runs while
the leader is still against it, so the walk ends on the touch and the party
then stands still); `heckran_prepare` and `heckran` then play the Medina gate,
the cave, a Shelter at its save point and Heckran. Heckran's whirlpool,
Leene Square's Gate (which shows Truce Canyon's pillar at the End of Time) and
that pillar lead to 600 A.D.; Guardia's kitchen gives Zenan Bridge's jerky,
and `zombor` plays the bridge. It ends with Zombor beaten (frame 698042). Lab 16's Shadows,
which no weapon hurts, are run from by their enemy number; any other fight
with no enemy the bot can wear down fails the route.

The other legs of the playthrough start from cartridge saves so the whole
game is played in parallel. `playthrough_leg-c` starts from ctsave08 slot 1,
Crono, Ayla and Marle at Tyrano Lair's save point, and plays through `play()`
of `black_tyrano`, `zeal_palace`, `mud_imp`, `giga_gaia`, `golem_twins` and
`ocean_palace_lavos`, with its own phases between them: the Lair Ruins' Gate,
the Golem and the exile, a dactyl to the Mystic Mountains' Gate, the End of
Time's 2300 A.D. pillar, Lab 32, the Sewer Access's switch, Belthasar's Epoch
to Algetty, Mt. Woe's islands, Dalton, the Ocean Palace's buttons, platforms,
elevator and floor switches. It ends as Lavos rises from the Mammon Machine
(frame 568835). The save's bag has no Tonics by Zeal, so its fights heal
with Lapis, Marle's Cure 2 and Revives, and use weapons (the Ocean Palace's
Scouts drink the element they are hit with); Shelters at Algetty's, Mt. Woe's
and the Ocean Palace's save points restore the party before Mud Imp, Giga
Gaia, Dalton and the Twins. No level or purchase was needed. The Ocean
Palace's platform statues take the field's control a moment at a time while
the pad still walks the party, so that stretch walks by position (measured in
ct_native only). The palace's elevator stands still after its first fight
until the orb on its left is confirmed, as the original does: checked with the
frozen `emulator-oracle` oracle from ctsave10 slot 2.
`playthrough_leg-d` starts from ctsave11 slot 1,
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
the Masamune in 1000 A.D., Medina's and Truce Canyon's Gates, 50 Mid Tonics
and 2 Revives from Dorino (the save has no Mid Tonics), the Cursed Woods, where Frog joins, the Magic
Cave (`magic_cave`'s piece) and Magus's castle: the castle's approach,
`castle_slash`'s and `castle_flea`'s pieces, the hall's trapdoor, Ozzie's
corridor and the inner rooms (a search that plays ladders and stairs through),
a rest with a Shelter at the save point in the dark room off the tower
(`$1D7`), Ozzie's last stand (the party strikes the levers beside him, not
Ozzie), a second rest at the save point in his hall, and Magus at levels
21-22: the policy reads his barrier's open element from his elemental
affinities and casts Lightning or Ice to match, and uses X-Strike only when
Frog is ready and Aura Whirl only when Marle is. After the Gate to
65,000,000 B.C. it plays Laruba's hut, Dactyl's Nest and Ayla's dactyls, the
flight to Tyrano Lair, `nizbel_ii`'s piece with a frugal policy and rests at
the lair's save points (its Shelters), the switch room's west part, and
`black_tyrano`'s piece to Lavos's fall (`tyrano_after`, frame 738065).

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

# The library's boss, event and side-quest routes, as recorded

The per-route accounts of the routes that recorded the library's boss fights,
story events and side quests (`tools/movies/routes/`), moved from
docs/MOVIES.md, which keeps the mechanism. Each says what the route plays,
which save it starts from (a stock one, or a cartridge save a preparation
route made through the game's own save menu), how it knows it reached its
outcome, what its shadow run covered over the library of the time, and how
`ct_native`, strict mode and the shipping-mode route check went. Numbers are
as measured when the route landed, each over its own narrowed run; the
library's timing was recalibrated once over all of them when they merged.

`motherbrain` (`battle-motherbrain.ctm`, 135481 frames) starts from fantasyanime
ctsave14 slot 2 on the 600 A.D. world map. It boards the Epoch, selects 2300 A.D.,
lands southwest of Geno Dome and puts Robo in the lead. The route clears the five
conveyor battles, opens the charging chamber, turns on the first and third code
switches, charges the doll and escort pods, and brings the escort robot to the
guard for the other doll. The robot follows the leader directly, so the route
keeps it around corners and balances the reversed conveyor with Down while it
catches up, checking the robot's position before advancing. It clears the
main-floor encounters before the timed escort charge. Map exits, event flags
and object positions supply the destinations; searches on machine copies keep
only their winning input. No RAM writes are used.

The existing `party` policy wins Robo's solo Atropos fight and MotherBrain's
fight, both without a retry in this recording. The route places both dolls,
plays the shutdown and return to the world map, then re-enters Geno Dome and
waits for field control. Its final checks require location `$0F8` and the
MotherBrain defeat bit (`$7F:013B` bit 4); stopping at the battle's last hit would
leave the ending scene and field load unfinished.

The route's verification from power-on reaches that outcome at frame 150806.
Its MotherBrain fight uses `policies.motherbrain`: prioritize Flare, Ice 2 and
Robo's magic before her repeated stat increases make a prolonged fight costly,
with healing as needed. That fight also wins without a retry.

The recording's shadow replay has 0 mismatches and no refusals or accessor-range
errors. It adds 1244 executed bytes beyond the other 48 movies, all already
ported, including MotherBrain's add-to-stats-then-tech action; see the coverage
and comparison limits in [movies-wanted.md](movies-wanted.md#5-boss-fights).
`ct_native` consumes all 135481 frames without a stop (nine screen setups and
four SNES-only video escapes, first at `$7E:5AEB`). Strict mode also reaches
the end; its saved game differs in 323 bytes, recorded as `drifts` in
`tools/strict_baseline.txt`. These fixed-input native runs drift from the
reference route's timing; the reference recording establishes the boss victory.

A third batch went looking for where the native program stops rather than
for code no movie runs: a throwaway route loaded every save point no movie
started from and walked out of its map and into the next, and another walked
up to the nearest objects until one attacked, and each run was played in
`ct_native` and on the hand ports alone (TODO.md, "The native program",
"Where the save movies stop"). What stopped, and a fight of a place no
movie fought in, became routes: `magic_cave` (`events-magic-cave.ctm`),
`last_village_12kbc` (`field-12kbc-after-the-fall.ctm`) and `denadoro_fight`
(`battle-denadoro.ctm`), the last walking into its fight with
`tools/movies/encounter.py` (the nearest objects in turn, the next map when
none attacks; and if that finds none, a wider search from the checkpoint
before it, for the shipping mode, whose timing has the monsters elsewhere).
A fight inside Magus's castle (ctsave06 slot 3) was found the same way but
left out: the way from the castle's hall into the next room (`$0A8`) that
the interpreter's search found, it found only at some frames, and in the
shipping mode at none.

`nizbel_ii` (`battle-nizbel-ii.ctm`) starts outside Tyrano Lair on ctsave07
slot 3, frees Kino, crosses the switch rooms and teleport maze, opens Nizbel
II's door, rests with a Shelter at his room's save point, and wins the fight.
The route uses the location exits and event scripts for its itinerary and
live field searches for each walk. Its maze search targets the exit warp's
destination so it cannot keep choosing a nearby return warp; at a corner
landing it tests a step away before asking the usual control check again.
`policies.nizbel_ii` resolves learned techs by id, uses lightning against the
boss's defence, and heals and restores MP with this save's supplies. The movie
ends after the victory messages with field control restored. See Targets, 5
in [movies-wanted.md](movies-wanted.md) for its coverage and validation.

`unequipped_armor` (`battle-unequipped-armor.ctm`, ctsave04 slot 2, 22859 frames)
reproduces the US equipment glitch: R+Up preserves the equipment list's member-plus-slot
cache key, allowing Marle to equip item zero in her armor and helmet slots. It crosses
Zenan Bridge and Truce Canyon, then beats Spekkio with Ice and ends with field control.
Her Ice deals 244-246; an equipped comparison from a machine copy deals 157-159.
Incoming Ice is 15 versus 108, Lightning 20 versus 171. The two empty slots give +20
magic and +30 Lightning/Shadow/Water resistance; Fire is unchanged. See
[Target 14](movies-wanted.md#14-the-originals-known-glitches) for the code and measurements.
It adds 0.49 KiB beyond the other movies. Shadow has 0 mismatches and no invalid
access or refused page; its 44 equipment-list guard refusals are documented gaps.
Strict stops at frame 7594 (`$C0:1AAC`, field confirm); `ct_native` stops at frame
3147 (`$C2:AC3A`, the inventory-list slide). Target 14 records the calibration and
narrowed checks too.

`luccas_mother` records `events-luccas-mother.ctm` (15701 frames): recover
Robo at Fiona's shrine, the campfire, the red gate to 990 A.D., save Lara
with the machine's timed L, A, R, A password, and return for Robo's
GreenDream. It ends on the 1000 A.D. world map with control back, checking
Lara's saved bit (`$7F:007C` bit 7), the gift and a movement probe on a copy.
The password search restores the console checkpoint between attempts:
three-frame taps miss the script's checks; eight-frame holds separated by
24 released frames succeed. Only the winning inputs enter the movie.
The bedroom's direct workshop exit reaches an isolated balcony; the route
goes through the front room to reach the console beside the machine.

Its save is `routes/luccas-mother-before-campfire.srm`, slot 1, made through
normal play from technoplaza `ct-state11`. `prepare_luccas_mother` plays the
Retinite and forest prerequisites and saves through the menu; it belongs to
`HAND_RUN_ROUTES`, with its 40031-frame movie outside the library. Its replay
reproduces the prepared SRAM byte for byte. The stock `ctsave13`
already has Lara rescued, and `ctsave12` has not opened the desert
([save provenance](../oracle/saves/README.md#routes)). Shadow is clean,
including sound lockstep; strict mode completes and matches the reference
end state. `ct_native` stops at frame 3120, recruiting Robo (`$C0:38BE`).
The [side-quest account](movies-wanted.md#12-the-side-quests) records its
coverage and the visibility-flag alias observed by the effects check.

`fionas_forest` (`events-fionas-forest.ctm`, 36440 frames) starts from
fantasyanime ctsave12 slot 3. The forest flags (`$7F:019E`) and Retinite's
defeated bit (`$7F:01A3` bit 0) are clear. The sapling's bit (`$7F:00F7`
bit 1) is clear too, so the route first visits the Last Village in 12,000 B.C.
and chooses to plant it. Crono, Robo and Marle cross the Sunken Desert,
win its central encounter and defeat Retinite: Ice 2 softens its defenses,
Confuse and Uzzi Punch kill the upper half, legs, then core. Keeping the core
alive avoids the fiercer attacks after its death. The route leaves Robo with
Fiona in 600 A.D., flies the Epoch forward 400 years, recovers him at the
shrine and plays the campfire through Lucca back in hand in 1000 A.D.
It ends before her optional gate to the past. Machine copies search the
moving sand and retry battles; only successful inputs become the movie.
The movie adds 828 bytes of executed code over the other 48 movies, all ported:
the sand and party trail, Retinite's stat condition, and event arithmetic and
party removal. Its dialogue table at `$FF:6B00` and its 185 strings join the
asset catalogue; the catalogue contains locations and types, not ROM data.
`ct_native` and strict mode both run to frame 36440 without stopping; strict
mode ends with 131 saved-game bytes different from the reference, so its
baseline is `drifts`. The departure's guard gaps are in TODO's "Refusals in
real play" and `tools/refusal_allowlist.txt`.
Shadow reports zero mismatches, no refused page or out-of-range accessor,
and no uncatalogued asset reads; its 120 guard refusals are the two documented
departure gaps. Sound lockstep matches all 111713 ticks, and the full
36440-frame generated-only trace matches the reference.
Timing costs are calibrated with `--drifted` over this movie; the eleven
remaining timing exceptions record the measured paths and limits. Its
calibrated shipping-mode vblank check has zero unexcepted findings.
The narrowed suite (`--movies events-fionas-forest -j 1`), including its static
checks, ends with `suite OK`.

`son_of_sun` (`battle-son-of-sun.ctm`, 16377 frames) starts at ctsave13 slot 3
and boards the Epoch on the save's starting tile. It flies to 2300 A.D., lands
east of the Sun Palace and wins Son of Sun, stopping with the field back in
hand. The save's quest byte, `$7F:013A`, is `$00`; ctsave14 slot 1 has `$F7`.
The flame policy reads enemy `$F7`'s reaction state after Roulette Shuffle:
stat offset `$3C` is `$62` on the real flame and `$63` on the counterattacking
ones. Before that choice is available it tries a flame; thereafter it attacks
the real one and uses healing items. The eye's AI ends the fight at 10000 HP
or less, from 12000, while the flames still have HP. The route checks that
threshold and then checks field control, rather than waiting for every enemy
to have zero HP. No machine writes are used.

`sun_stone` is the optional hand-run extension, listed in `HAND_RUN_ROUTES`:
from the same save, win the fight, collect the Moon Stone, return to the Epoch
and fly to 65,000,000 B.C.'s Sun Keep (`$147`). Leave the stone there and return
to field control, with `$7F:013A` at `$07` and the Moon Stone removed from the
bag (22857 frames). The charging starts in prehistory; 2300 A.D.'s Keep is
where the charged stone is eventually collected. The intervening theft and
recovery are not played. Its movie repeats the boss fight and stays outside
the library, for example `--movie build/sun-stone-hand-run.ctm`.

`rust_tyrano` (`battle-rust-tyrano.ctm`, 23282 frames) starts at Giant's
Claw's save point in 600 A.D., opens the nearby door and defeats Rust Tyrano
with Crono, Lucca and Robo. It includes the countdown and fire breath,
Confuse and Uzzi Punch, healing and MP restoration, and stops with the field
back in hand in the shell's chamber (`$0C5`). A fresh route run reproduced
the movie's inputs. Shadow replay has zero mismatches, refusals, refused
pages or out-of-range accessors. Its coverage adds 54 instruction bytes
(0.05 KiB, all ported) over the preceding 48 movies: field walking, healing
and battle reward branches (Targets 12 in movies-wanted.md).
The shipping-mode route (`on:on:c`) also wins without backtracking, at
frame 23105, and returns field control at 24605, with zero fallback
instructions or guard refusals. It restores Crono's MP for Life 2 if a
party member falls; that recovery branch is unused by the recorded movie.
`ct_native` runs all 23282 frames without a stop (four existing video
escapes, first `$7E:5AEB`). Strict mode also reaches the end without a
stop; its saved game differs in 72 bytes of inventory counts and character
records, recorded as `drifts` in the strict baseline. The narrowed gate
requires 177 timing constants recalibrated with `--drifted` and an
uncompared baseline entry for `battle_apply_damage` (67 interrupted calls,
56 matched). Two vblank-tail exceptions cover `battle_record_trails` and
`field_nmi`; the movie's video check finds no writes moved onto the picture
(TODO.md records the estimates to refine). The combined library needs its
own timing measurement.

Its starting save is `oracle/saves/routes/rainbow-shell.srm`, slot 1, made
through the game's own save menu after a Shelter. `rainbow_shell` plays the
long approach from fantasyanime ctsave13 slot 1: the Epoch to 600 A.D.,
the ruined throne, cave encounters, floor switches and pits, then the save
point. The recorded approach takes 55184 frames; it is a hand-run route
(`HAND_RUN_ROUTES`), with its movie kept outside the library. It exports
the cartridge SRAM as `<prefix>.srm`; a fresh preparation run reached the
save point at frame 53687. Replaying the original 55184-frame inputs
recreated the archived SRAM exactly. Neither route uses memory pokes.
The archive's slot 2 is already after the boss and shell delivery; slot 3
is after the trial. The delivery and trial are not in this movie.

`cyrus` (`events-cyrus.ctm`, 39872 frames) starts from ctsave14 slot 1,
before the Northern Ruins repairs. It takes the carpenter's request in
600 A.D., borrows the tools from his counterpart's family in 1000 A.D.,
returns them and commissions the repairs. The three left-wing guardian
groups are beaten, that floor is repaired for 2000 G, and Frog replaces
Crono for Cyrus's grave: the ghost, Masa and Mune, and the Masamune restored.
The route checks the repair and completion flags, Frog's upgraded weapon,
and player control at the end. Slot 2, used by `northern_ruins_switch`, is
already after this quest. The right-wing treasure rooms need no visit.
Its waypoint searches use machine copies and retain only successful walks;
the fights use the `party` policy. Shadow has 0 mismatches and no refusals;
strict mode reaches the end with five character-record bytes differing.
The route also reaches its checked outcome in shipping mode
(`--native on --generated on --sound-driver c`), at frame 38104.
`ct_native` consumes all frames without a reported stop, but ends blanked,
so that run does not establish the quest's outcome. See Targets, 12 in
[movies-wanted.md](movies-wanted.md) for the coverage and native stops.

`ozzies_fort` (`battle-ozzies-fort.ctm`, 39237 frames) starts from fantasyanime
ctsave12 slot 2 at the End of Time. The fort flags at `$7F:01A1` are `$00` in
slot 2 and `$FF` in slot 3, including the final trap's bit 7. It boards the
Epoch, selects 600 A.D., lands east of the fort, and plays Flea Plus, the
conveyor scene, Super Slash, the treasure trap (walking past the chest), the
three generals, the switch battle, the fall downstairs and the return for
the cat to spring the final trap. It ends with all eight fort bits set and
the field back in hand. Lucca casts Flare against the trio while Crono and
Marle heal between its counters; machine-copy retries keep only the winning
inputs. The route stops on each event's trigger row before waiting for its
dialogue, since the animation can start before the window opens.
Against the preceding 48 movies, its coverage adds 393 bytes (0.38 KiB), all
ported: chiefly single-tech learning and the rewards that announce it, plus
event flag setting and the Epoch arrival. Shadow has no mismatch or refusal;
the C sound driver's state matches at every tick. Strict mode reaches the
frame budget with different saved-game state as the inputs drift.
`ct_native` reports no stop over the same budget but leaves its display
blanked; that playback does not establish completion of the fort (TODO.md).
The narrowed timing calibration leaves the screen-command dispatch's varying
work at 11 of 1029 vblank stretches over 1024 clocks, at most 1949 clocks,
with no drift; `timing_exceptions.txt` records it beside its channel and
frame runners. The movie's vblank check finds no unallowed write moved.
Interrupts leave 113 of 210 tile-row decodes uncompared; the existing isolated
whole-row fuzz test compares 10000 states with no mismatch or skip (seed 1),
recorded in `uncompared_baseline.txt`.

`yakra` starts at ctsave01 slot 2, outside the Cathedral in 600 A.D. It loads
story progress `$12`, finds Leene's pin, defeats the Naga-ettes, recruits Frog,
plays the entrance organ, crosses the guarded halls and both switch rooms,
plays the inner organ, and defeats Yakra. The 71287-frame recording ends in
`$0C6` with the field back in hand and `$7F:01ED = 1`; returning to the castle
with Leene is a later interaction. Its shared loading, encounter handling,
settings and early-party battle policy live in `routes/_early_bosses.py`.
Shadow is clean, and the route adds 1036 instruction bytes beyond the
previous library. `ct_native` reaches the limit with its screen blanked;
strict reaches it with a 128-byte saved-game difference. The coverage and
remaining native limitations are recorded in `movies-wanted.md` and `TODO.md`.

This is a hand run: `yakra` is in `HAND_RUN_ROUTES`, and
`oracle/movies/hand/battle-yakra.ctm` is outside the default movie library.
Movies in `hand/` are selected only by an explicit `--movies <name>` in the
suite and strict checker, so the long recording can still receive the same
narrow checks without joining the coordinator's default gate.

```sh
python3 -m tools.movies.routes.yakra --prefix build/yakra/run \
    --movie oracle/movies/hand/battle-yakra.ctm
python3 tools/suite.py --ct-dev build/main/ct_dev -j 1 --movies battle-yakra
```

`dragon_tank` (`battle-dragon-tank.ctm`, 16042 frames) uses the same early-boss
helpers. It loads ctsave02 slot 1 in the prison tower (`$01D`, story `$2E`),
walks onto the bridge (`$01C`), continues after the arrival scene, and defeats
the Tank's head before its other parts with physical attacks. The movie ends
after the destruction scene, with field control and `$7F:0199` bit 2 set.
Shadow is clean; `ct_native` and strict mode reach the frame limit, with the
strict saved game differing from the reference by 35 bytes. Its 631 new
instruction bytes are mostly the Tank's large field and battle sprite paths
(see Targets 13 in `movies-wanted.md`).

`zombor` (`battle-zombor.ctm`, 26320 frames) shares the early-boss helpers.
It loads ctsave04 slot 1 at story `$54`, delivers the Jerky supplies, agrees
to defend Zenan Bridge, defeats both groups of Ozzie's troops, and defeats
Zombor. Ice targets the upper body; Fire and Lightning target the lower.
It ends on the bridge with field control and story `$5A`. Shadow is clean;
the 369 new instruction bytes include enemy marching and battle inventory
removal. Strict stops at frame 20301 on the `field_press_confirm` guard;
`ct_native` reaches the frame limit with its screen blanked. Both gaps are
recorded in `TODO.md`.

`nizbel` is also a hand run, recorded as
`oracle/movies/hand/battle-nizbel.ctm` (57673 frames). From ctsave05 slot 2,
story `$7B`, it enters the Reptite Lair, descends the three holes and wins
the encounters through the lower rooms and hall. Crono's Lightning lowers
Nizbel's defense; the party uses physical attacks, Aura and the items in
the battle menu. The movie wins at 54768 and ends in Ioka (`$113`) after
recovering the Gate Key and Ayla's farewell, with story `$7D` and the field
back in hand. The route is in `HAND_RUN_ROUTES`; select `battle-nizbel`
explicitly to run its narrowed suite checks.
Its rebuilt shadow run is clean after adding the existing prehistoric
dialogue table's catalogue metadata. `ct_native` reaches the limit with its
screen blanked; strict reaches it with a 128-byte saved-game difference.
Its 385 new instruction bytes and the native limitation are recorded in
`movies-wanted.md` and `TODO.md`.

Check all four recordings, including the two hand runs, with one game
process at a time (set `CT_ROM` to the existing cartridge image):

```sh
python3 tools/suite.py --ct-dev build/main/ct_dev --rom "$CT_ROM" -j 1 \
    --movies battle-yakra battle-dragon-tank battle-zombor battle-nizbel
```

`fair_clone` (`events-fair-clone.ctm`, ctsave14 slot 1, 8335 frames) pays
40 silver points at Norstein Bekkler's tent, copies all 22 poses and returns
to the lower square with control restored. The route reads the pose and
answer-window variables and presses R, L, A or Y accordingly. Bekkler's
object `$0B` function 1 accepts confirm from below, around `(134,149)` facing
up, while local variable `$0B` is zero. His move to tile `(8,5)` precedes his
greeting; the route waits for it, then for the choice window to accept input.
The tent entrance is lower-square tile `(43,29)`. Short waypoint searches
keep the large square's machine-copy count bounded.

`fair_strength` (`events-fair-strength.ctm`, ctsave14 slot 1, 4276 frames)
rings the strength-test bell and wins one silver point. Confirm below object
`$1A` starts the leader's back-and-forth movement; a second A press chooses
its strength. The route searches a machine copy for local `$14` reaching 21,
then replays just that delay and the winning press. It ends with the square
back in hand and verifies that silver points rose from 131 to 132.

`fair_shuffle` (`events-fair-shuffle.ctm`, ctsave14 slot 1, 6866 frames)
pays 10 silver points and follows the requested soldier through Bekkler's
shuffle. Local `$0C` names the soldier, whose object identity (`$14`-`$16`)
survives the permutation. The route identifies soldier 3, verifies Crono's
Poyozo Doll flag, and ends outside with the field back in hand.

`fair_rescue` (`events-fair-rescue.ctm`, ctsave14 slot 1, 7681 frames)
pays 80 points, pushes all three monsters back with the rock launchers, and
rescues the companion. It checks the companion's fall counter and uses the
candy at 190 (four times in this recording); clearing the monsters alone
does not prove a win. The route verifies the cat-award flag and returns to
the lower square, whose cat-care arithmetic now runs on entry.

`giga_gaia` (`battle-giga-gaia.ctm`, 33261 frames) starts from ctsave09 slot 3,
on Mt. Woe at story `$B4`, before the summit's arrival flag (`$7F:01A0` bit 3).
It crosses the last bridges, beats Giga Gaia and its regenerating arms, frees
Melchior and watches the mountain fall, then closes the Terra Cave conversation
until the field is back in hand (`$17E`, story `$BA`). Its checkpoint search
includes the map level in each position: a bridge and the path under it lead
to different places. At the northern chain's end it holds UP into the exit;
at the summit it approaches the crystal and confirms. The battle policy uses
Lightning 2, Ice 2 and Fire 2 against the arms, Spincut against the head, and
the save's healing items. The recorded fight wins without backtracking.
Shadow replay is clean, including sound lockstep, with no guard refusals;
coverage adds 137 instruction addresses (301 bytes) to the previous 48 movies,
including the overworld object drift used during the mountain's fall.
Strict mode reaches the frame limit with saved-game drift. `ct_native` reports
no stop but ends blanked after three screen setups; this is an open follow-up
in TODO.md, not evidence that it completes the scene.

`zeal_palace` (`events-zeal-palace.ctm`, 20011 frames) starts from ctsave08
slot 2, in the snowfield at story `$9F`, with Schala's meeting flag
(`$7F:00F4` bit 4) clear. Slot 3 is already at `$AB`, after exile. It takes
the Skyways and land bridge, then the mountain's two paired teleporters to
the palace. Short overworld steps reach the entrances; releasing UP in each
mountain beam avoids stepping back into its return beam. Directed walks
through the palace reach Schala and Janus, then the sealed-door hall. The
route checks the meeting flag and story `$A2` after Schala opens the door,
then enters the Mammon Machine room and confirms before it to charge the
pendant. It ends at `$14B`, story `$A5`, with the field back in hand.
It adds 190 instruction addresses (423 bytes) beyond the preceding 49 movies,
chiefly the Skyway capsule irises (`$C6:E448`-`$E567`) and green palette
brightening (`$FD:E98F`-`$E9C9`). The irises expose 77 guard refusals at
`$C6:E498`/`$C6:E4BE`, recorded as gaps in the refusal allowlist and TODO.md.
The new dialogue reads select the `$F9:B000` pointer table and its 443 strings
into the asset catalogue; the catalogue holds their addresses and types.
Shadow replay has zero mismatches, refused pages, out-of-range accessors or
uncatalogued reads, and sound lockstep is identical. Strict mode stops at
frame 4109 on `$C6:E4BE`; `ct_native` stops at frame 4106 on the same iris.
The effects baseline records six runtime reads of the circle's centre and
radius words: the capsule setup writes them plainly, its guard bounds both
below 256, and the circle's scaffolding writes those same values back.

`magus_joins` (`events-magus-joins.ctm`, 13663 frames) starts from fantasyanime
ctsave11 slot 2 in the Earthbound Village, walks to North Cape, plays Magus's
flashback, refuses the fight and recruits him. It keeps his default name,
closes the party screen and stops with the field back in hand. The save's
story progress is `$D2` and `$7F:0138` is zero; ctsave11 slot 3 and ctsave12
already have recruitment bit 0 set. `north_cape` contains the shared route
and checkpoints the fight question so its branches can be tried independently.
Its new coverage against the preceding 48 movies is 83 bytes, all ported,
including the event's flags selected by number (`$C0:6B20`-`$6B4A`) and
screen-tint paths. The flashback's hidden-leader guard refusal is a recorded
gap in TODO.md; no game logic was changed. The dialogue resources it newly
reads are catalogued as metadata, with their bytes remaining in the user's ROM.
`ct_native` stops at frame 8531 and strict mode at frame 8561, both at
`$C0:881E`, before the fight/refuse choice.

`north_cape` (`battle-magus-north-cape.ctm`, 28653 frames) takes the same
opening, accepts the fight and defeats Magus with Marle, Lucca and Robo.
The party policy wins at frame 26953, without a retry; the movie closes the
aftermath and ends with North Cape back in hand and `$7F:0138` bit 1 set.
It adds 31 executed bytes against the preceding 48 movies, including eight
in the rewards' HP/MP growth path (`$C1:F8FC`-`$F903`) that recruitment does
not reach. Recruitment retains 60 exclusive bytes alongside this movie;
the pair adds 91 bytes, all ported. Both shadow replays have 0 mismatches,
no `page refused`, `accessor out of range` or uncatalogued asset reads.
Each has 164 guard refusals at the documented hidden-leader gap, and both
stop at the same frames and entry in `ct_native` and strict mode above.
The narrowed gate (`tools/suite.py -j 1 --movies events-magus-joins
battle-magus-north-cape`, with the built `--ct-dev` and user's `--rom`)
ends `suite OK`, including its static checks. Timing costs were calibrated
with `--calibrate game/registry.c --drifted` over the pair; the measured
tails are recorded in `tools/timing_exceptions.txt` and TODO.md.

`mud_imp` records `battle-mud-imp.ctm` (37817 frames) from ctsave09 slot 2,
Terra Cave's settlement. Its story counter is `$AE`, below the boss script's
`$B4` threshold; slot 3 is already `$B4`. The route walks into `$184`, wins the
two Beast encounters, then focuses Lightning, Ice and Fire on Mud Imp while
healing with Mid Tonics and Lapis. The Imp's defeat makes both coloured Beasts
leave. It stops with the field back in hand, without climbing Mt. Woe.
Shadow is clean, including sound lockstep, with no refusal, `page refused` or
`accessor out of range`. It adds 570 executed bytes beyond the preceding 48
movies, all ported. Strict mode runs every frame but ends with different saved
game data (`drifts` in its baseline). `ct_native` reports no explicit stop in
37817 frames, but finishes blanked with two video escapes, first `$7E:5AEB`;
that fixed-input run does not establish that the native program plays the fight.
The narrowed gate recalibrates 199 drifting registry costs. Its remaining
`field_nmi` vblank-tail finding is recorded in `tools/timing_exceptions.txt`
and TODO.md; the video-write check has no failing findings. The fallback
baseline needs no increase for this narrowed run.

`heckran` records `battle-heckran.ctm` (19376 frames), loading slot 3 of
`oracle/saves/routes/pre-heckran.srm`. The hand-run `heckran_prepare` takes
ctsave03 slot 3 through Medina and the cave, then saves normally at the cave
save point (42723 frames; its long movie stays out of the library). The
source's story counter is `$4D`, the new save's `$4E`, and the boss requires
less than `$51`; ctsave04 slot 1 is already `$54`. The boss route uses basic
magic, Mid Tonics and Ethers, and waits whenever Heckran's script counter
(`$7E:B320`) marks its water counterattack stance. It waits twice, wins at
17266 without a wipe, and stops after the aftermath at `$02F`, story `$51`,
with field control. Shadow is clean, with no refusal, `page refused` or
`accessor out of range`. Coverage adds 74 bytes beyond the preceding 48
movies, all ported; 62 remain unique after adding Mud Imp, including the
enemy's flags-turn tail (`$C1:9E58`-`$9E5B`) and field sprite drawing.
Mud Imp retains 558 unique bytes after Heckran. Strict runs all frames but
ends with different saved game data (`drifts`). `ct_native` reports no
explicit stop in 19376 frames, with a screen showing and four video escapes,
first `$7E:5AEB`; this does not establish a native boss victory.
Adding Heckran recalibrates 120 further drifting registry costs over both
movies; their combined timing check passes without further exceptions.
The narrowed fallback baseline adds the menu task-start helper `$C2:8249`
(14 instructions, 12 bytes) and four bytes of number formatting at
`$C2:F213`-`$F216`; both are outside a registered comparison in reference
play. Existing library counts are retained. Shipping-mode fallbacks and
guard refusals remain zero.
`trial` records `events-cronos-trial.ctm` (21214 frames): bringing Marle home,
the arrest, courtroom testimony and fair flashbacks, the jury's 5–2 acquittal,
three days' solitary confinement, and the prison transfer, ending with Crono
awake in his cell (`$047`, story `$2D`, trial phase `$10`) and responding to
the pad. It loads slot 1 of `oracle/saves/routes/cronos-trial.srm`, a normal
cartridge save made outside Guardia Castle by the long route below.

`cronos_trial` is a hand-run route: ctsave01 slot 2 is before the Cathedral,
and slot 3 is already after the trial. Its 136868-frame movie is
`oracle/movies/hand/events-cronos-trial-long.ctm`, outside the default library,
and the route is in `HAND_RUN_ROUTES`. The Cathedral stages and early battle
policy come from `early-bosses`' Yakra route. The continuation restores Leene,
reunites Crono and Marle, plays Frog's farewell, crosses Guardia Forest and
Truce Canyon, takes the gate back to 1000 A.D., and returns Marle to the castle.
The game's overworld Save menu writes slot 1 before the arrest; the route
exports that SRAM to `<prefix>-before-trial.srm` for a fresh `trial` recording.
Machine-copy searches keep only successful walks; neither recording uses
diagnostic writes. The castle's spiral stairs reuse a location number, the
forest's two exits share a destination number, and the courtroom testimony
temporarily revisits the fair, so their route checks account for those cases.
The Cathedral's `strings_f9b000` catalogue metadata is also reused from
`early-bosses`, so native asset reads recognize that dialogue pool.

Both recordings pass shadow with zero mismatches, no `page refused`, no
`accessor out of range`, and matching C sound-driver lockstep. Coverage against
the preceding 48 movies adds 9 live code bytes for the short movie
(`event_cmd_wait_frames`) and 1480 for the long one, all already ported.
The latter adds 444 bytes beyond those movies plus `battle-yakra`, principally
counted movement towards objects and party members and the overworld's region
song. The courtroom and jury are new event-script coverage even though their
engine code was already exercised. Six guard gaps are listed in
`tools/refusal_allowlist.txt` and TODO's "Refusals in real play".
The narrowed shipping-mode vblank check reports zero findings outside the
existing exceptions. Timing was calibrated with `--calibrate game/registry.c
--drifted` over these two shadow logs; the remaining data-dependent tails
and minimum-scalar drift are explained in `tools/timing_exceptions.txt`.

`ct_native` stops the short movie at frame 20629, `$C0:881E`, "a walk the
port doesn't take", during the prison transfer. The long movie reaches its
136868-frame limit with a blank screen and no explicit stop; that does not
establish that it completed the story. Strict mode reaches both frame limits
but drifts from the reference saved state (`tools/strict_baseline.txt`).

Both hand movies and library movies can be named explicitly in the narrowed
suite; hand movies remain excluded when no names are supplied. With `-j 1`,
the two trace-identity replays run sequentially too.

`castle_slash` (`battle-castle-slash.ctm`, 39278 frames) starts from ctsave06
slot 2, visits both wings of Magus's castle, wins Ozzie's ambush, and beats
Slash's henchmen and both of Slash's forms. It ends with the field back in
hand. Slot 3 is already after both castle bosses: `$7F:00A3` is `$7E`, with
Slash's `$04` and Flea's `$08` defeat bits set; slot 2 has `$00` and story
counter `$87`. The shared castle search tries machine copies nearest the
door's position first, accepts the destination map rather than a timed walk,
and waits out requested room changes. Losing field control is a separate
outcome: the entrance scene pauses before opening dialogue, which a search
deduplicating only positions would discard. Once Ozzie has appeared it first
searches for a way around corridor ambushes. Only the winning inputs are
replayed and kept. The shipping mode reaches Slash's defeat and field control
at frame 40222 (`route_check castle_slash`, `on:on:c`).

`castle_flea` (`battle-castle-flea.ctm`, 35256 frames) uses the same slot 2
and approach, then takes the right passage (`$0AB`) and corridor (`$07D`)
to Flea's room (`$0AD`). It beats the decoy and Flea, checks the `$08`
defeat flag, and ends with field control restored. The shipping route reaches
that outcome at frame 40035, with exact machine-copy restores. Both castle
movies keep the searches' successful input only; neither uses memory pokes.

`r_series` (`battle-r-series.ctm`, 65437 frames) takes ctsave03 slot 2 from the 2300 A.D.
world map through the Factory Ruins' lab security doors and fights. The
location exit lists and the scripts for `$0E5`, `$0E6` and `$0EB` establish
the path: the upper lab console opens the ladder, the lower console opens
both security doors, and the basement code reader accepts X, A, B, Y without
requiring the warehouse cranes. The route holds each code button until its
acceptance variable changes. Restoring power sets story `$3F`; escaping the
shutters and returning up the lab corridor starts the R Series scene.
The `r_series` battle policy uses Crono and Lucca's Fire Whirl and Cyclone,
with the early save's healing items. The movie continues through Robo's
rescue and repair and the Gate trip, ending with the field back in hand at
the End of Time. Its battle-start check requires all six initialized robot
stat records, since the menu byte alone briefly looks ready during loading.

### The Ocean Palace finale

`golem_twins` starts from fantasyanime ctsave10 slot 3. Crono, Marle and Lucca
alternate Lightning 2, Ice 2 and Fire 2, hitting both Golems and changing what
they copy. The recorded fight wins without backtracking at frame 15912;
Dalton's departure returns control at 19102. The route returns to the save
point, saves slot 3 and closes the menus. `battle-golem-twins` is 20216 frames;
it adds no instruction bytes beyond the other movies, but records the victory
and produces the next movie's cartridge save. The route writes `<prefix>.srm`
from battery RAM after saving; the interpreter's copy is committed as
`oracle/saves/recorded/ocean-palace-after-twins.srm` (CRC-32 `1D74BE21`).

`ocean_palace_lavos` loads that save and records `events-ocean-palace-lavos`,
30364 frames. Through the cleared Twins room (`$19E`) to the Mammon Machine
(`$19F`), speak to the Nu blocking the corridor and approach the machine.
Lavos rises at frame 8169 (`$1D3`); the scripted loss leads to `$1A7`, where
Crono's last walk is in hand at 14219. Walking forward starts his fall;
Schala sends the survivors away, and Zeal's destruction starts on `$1F4` at
20338. The party reaches the Last Village hut (`$1A9`) at 26248, wakes, and
accepts Marle, Lucca and Robo in the party exchange. The movie ends after
the hut reloads and movement is verified, with story counter `$CD`.

`--coverage` records 104088 instruction bytes for the Twins and 98326 for the
finale. Against existing coverage from the other 49 movies, the finale adds
1037 bytes (1.01 KiB),
all credited to ports by the static coverage measure: the capsule iris and
world-map object-script work (`$C6:E448`-`$E6CF`, `$C2:16FA`-`$1758`,
`$C2:191F`-`$1946`, `$C2:7BD1`), event movement and item removal, and field
drawing. Four guards still refuse real calls in Zeal's fall; their gaps are
in [TODO.md](TODO.md#refusals-in-real-play). The finale also first uses the
dialogue tables at `$F9:B000` and `$FF:6B00`: the tables and their 443 and 185
strings are now catalogued by range, without including their bytes. Splitting
at a real save keeps both movies below 40000 frames; no machine state or
memory edit is a movie input.

Both movies' shadow runs have zero mismatches, page refusals or out-of-range
accessors. Strict mode (`--native on --generated off --no-interpreter strict
--sound-driver c`) reaches both final frames, but differs from the reference
in 58 saved-game bytes each and, for the Twins, 224 SRAM bytes: both have
`drifts` baselines. `ct_native` reaches both frame counts (exit 0) without a
missing-C stop, but reports four/two unsupported video calls, first at `$7E:5AEB`;
the finale ends blanked. That playback gap is in TODO.md, "The native program".
Timing is calibrated with `--drifted` over this pair; the fallback baseline
retains the library's existing rows and adds the newly reached fallbacks.
The narrowed gate, `python tools/suite.py --ct-dev build/main/ct_dev.exe -j 1
--movies battle-golem-twins events-ocean-palace-lavos` (with the ROM supplied
by `--rom`), ends with `suite OK`, including the static checks. Native-shape
playback completes both movies, and sound lockstep is identical.
## Death Peak and Crono's return

Three movies keep this climb short enough to replay separately. Both original saves have
the Chrono Trigger, the clone and the Keeper's instructions completed; neither has beaten
a Lavos Spawn. The routes check those prerequisites rather than changing the save.

| Movie | Route and start | Frames | End |
|---|---|---:|---|
| `events-death-peak-wind` | `death_peak_wind`, ctsave11 slot 3 | 4676 | Both tree shelters passed; field beyond the wind in hand |
| `events-death-peak-ascent` | `death_peak`, ctsave12 slot 1 | 23852 | Two Lavos Spawn beaten, cave opened, slippery ridge crossed; saved at the last save point |
| `events-death-peak` | `death_peak_return`, routes/death-peak-summit slot 1 | 23856 | Third Spawn beaten, shell climbed, Time Egg and clone used; Crono returned and End of Time field in hand |

The wind route searches departure delays on machine copies. The first tree needs the
party to stand still during a gust; at the second it must keep pressing UP to stay sheltered.
The ascent searches paths to named cave entrances and ledges, checking both the destination
map and proximity to the intended entrance because several exits share a destination map.
All three Spawn fights target only the head: Marle's Ice, Lucca's Fire and Robo's Uzzi Punch,
with healing and Ether when needed. All three won on their first battle attempt.

The ascent makes `oracle/saves/routes/death-peak-summit.srm` through the game's save menu;
its route also exports that cartridge SRAM beside its scratch checkpoints. The return route
starts there, pushes the empty shell under the ladder (tile 16, 15), and climbs it. On the
narrow landing only UP moves, so it walks off the landing before testing for field control.
At the summit the Time Egg takes the party to the frozen Ocean Palace. The clone is exchanged
for Crono, the return scene finishes, and the route verifies his return flag and the two
quest items' removal. Machine copies and input logs stay in scratch storage; the library
contains only the successful controller inputs and the cartridge save.

Coverage against the cached reference coverage of the preceding 48 movies adds 2153 ROM
instruction bytes across the batch. Individually the wind, ascent and return add 154, 1071
and 1699 bytes against that earlier library; against the earlier library plus the other two
new movies their unique contributions are 0, 445 and 1014 bytes. The wind remains the only
movie playing both tree shelters and the guide's transformation. The ascent uniquely
exercises single- and combination-tech learning in the Spawn rewards (`$C1:F39A`, `$C1:F4DF`,
`$C1:F533`, `$FD:AD17`). The return adds the circular BG3 screen effects (`$C0:2514` onward),
shell contact and movement paths, quest-item removal and the large field-object drawing
paths (`$C0:C820` onward). Its dialogue also needs the already-described `$F9:B000` string
table and its 443 text resources added to the asset catalogue; these are metadata only.

`ct_native` reaches all three frame limits with exit 0 and no stop: wind/ascent/return
report 2/4/4 video escapes, first at `$7E:5AEB`, and end with the screen blanked/blanked/showing.
Its own frame timing does not establish that these fixed inputs reached the route goals.
Strict mode (`--native on --generated off --no-interpreter strict --sound-driver c`) also
reaches every frame limit without a stop. Wind ends as the reference does; ascent and return
drift (78 and 89 saved-game bytes, plus 79 and 208 SRAM bytes respectively), recorded in
`tools/strict_baseline.txt`. The fallback baseline needs no increase. The narrowed run's
tile decoder has 150 interrupted calls and 126 matching calls, with none abandoned; its
uncompared-baseline reason also records the preceding library's 6190 matching calls.
All three shadow replays are clean: zero mismatches or guard refusals, no `page refused`
or `accessor out of range`, and identical sound lockstep. The return's generated-only trace
matches the interpreter for all 23856 frames. Drift-only calibration updates 239 timing
estimates for this narrowed run; `field_nmi`'s remaining 1.60% vblank tail (maximum 4129
clocks) is documented in the timing exceptions and TODO, with no newly visible video writes.
The suite's trace pair now also respects `-j 1`, so this gate runs one game process at a time.
The narrowed `suite.py -j 1 --movies events-death-peak-wind events-death-peak-ascent
events-death-peak` run, including its static checks, ends `suite OK`.
## Dalton Plus and the Epoch's wings

`dalton` records `battle-dalton.ctm` in 39666 frames from
`recorded/blackbird-before-dalton.srm` slot 1. The save was made through the
cell's normal save-point screen after the opening already played by
`events-blackbird`, all three equipment chests, and the inventory chest.
The `blackbird_save` preparation route takes 74786 frames and is in
`HAND_RUN_ROUTES`; its recording stays outside the library. The save's
provenance and starting flags are in [the saves README](../oracle/saves/README.md).

The movie takes the ducts to the upper deck, opens the west bulkhead, wins a
Byte encounter, and crosses the wing by running against its downward drift.
Golem Boss falls at frame 21922. Dalton's Epoch reveal follows, then Dalton
Plus (frame 26546; defeated at 33954), the controls tried with Y and X, and
the Blackbird's destruction. The landing conversation ends on the 12000 B.C.
world map (`$1F6`), with walking control verified by a step. Blackbird stage
`$7F:00D2` is `$0A`, and the wings bit `$7F:00BA & $80` is set.

Shadow reports zero mismatches, refused pages, out-of-range accesses, or
uncatalogued reads. The 20 guard refusals at `field_walk_the_leader`
(`$C0:881E`, from frame 26263) are a documented gap: Marle's jump onto the
Epoch takes her Y above the guard's bound. The allowlist and five new
fallback rows record it; no other fallback is new or increased. Coverage
adds 392 ROM bytes (201 instruction addresses including RAM) beyond the
other 49 library movies, including `event_cmd_set_flags` (`$C0:6BF7`),
in-view conditions, direction-to-target, and layer-scroll paths. The
previously declared `$FF:6B00` dialogue table and its 185 strings are now
catalogued; only metadata is committed.

`ct_native` consumes all 39666 frames without an explicit stop, but reports
only three screen setups, ending with the screen showing; completion is not
established. Strict mode halts at frame 25171 at `$C0:881E`, before Dalton
Plus. Drift-only calibration updates 226 estimates; one residual scheduler
timing exception is documented in TODO.md, with no unlisted video-write
movement. The scrolling command's 184 abandoned comparisons (of 186, two
matched) were checked with its existing fuzz constraints: 2999 matches,
zero mismatches, 46875 guard skips and 126 call-out budget skips in 50000
cases.

The route was also checked from the prepared cartridge save without its
development checkpoints, reaching walking control in 39824 frames. Its final
control check tries another direction when the landing spot blocks the first.
The narrowed suite, including its static checks, ended `suite OK`.

## Robo repaired at Proto Dome

`proto_dome` records `events-proto-dome.ctm` from ctsave03 slot 1 in 24717
frames. Its approach reuses the winning Lab 32 race inputs, then enters Proto
Dome, wins both Bugger encounters, examines Robo, and plays Lucca's repair and
his introduction. It accepts the name Robo and takes Lucca along, ending with
field control in Proto Dome and party Crono/Robo/Lucca. The starting story
counter is `$37`, with repair stage `$7F:0148 = $00` and Robo absent; the end
is story `$3C`, repair stage `$0F`. Slot 2 is already after the repair.

Shadow clean: zero mismatches, guard refusals, refused pages, out-of-range
accesses, or missing catalogue entries. Coverage adds 147 ROM bytes (71 new
instruction addresses including RAM) beyond the original 48-movie library,
notably `battle_condition_other_kind_present` (`$C1:9656`) and
`battle_condition_is_enemy` (`$C1:8F87`), plus the field's dash-button condition
and animation line limit. The fallback check found no new or increased use.
`ct_native` consumes all 24717 frames without reporting a stop, but ends blanked
after four screen setups; completion of the repair is not established.
Strict mode consumes every frame without a halt but ends in Proto Dome at
story `$37`, repair stage `$00`, with Robo still absent (59 saved-game bytes
differ).
The 60 interrupted tile-row decoder calls (of 102) were checked separately:
5000 differential-fuzz cases with the existing constraints all matched, none
skipped; the other 42 movie calls matched in shadow. Drift-only timing
calibration updated 204 estimates; four residual exceptions are documented in
TODO.md, with no unlisted video-write movement. The narrowed suite, including its static checks, ended `suite OK`.

## The Rainbow Shell trial as a hand-run route

`rainbow_shell_trial` starts from ctsave13 slot 2 and reaches the King's
exoneration with the castle field back in hand at frame 47547. It visits the
court, retrieves the shell fragment, exposes the Chancellor, defeats Yakra XIII
(at frame 41423), and plays Marle's reconciliation with her father. The route
checks the shell-delivered flag, the fragment flag, the trial stage, and the
King-exonerated flag; it uses only controller inputs, with machine copies for
walking searches and battle retries.

The recording remains outside the library: it exceeds the short-movie target,
so `rainbow_shell_trial` is in `HAND_RUN_ROUTES`. To make it locally:

```sh
python3 -m tools.movies.routes.rainbow_shell_trial --prefix build/trial/route --movie build/trial/events-rainbow-shell-trial.ctm
```

The original settings took 48156 frames. Choosing the fastest battle and
message settings in the game's menu and confirming dialogue more often gave
47547. An attempt to make a nearer cartridge save after collecting the evidence
did not find a working path out of the castle; no such save was exported.
The remaining work is a shorter route or a save made through normal play before
the final hearing (see [movies-wanted.md](movies-wanted.md), Targets, 12).

Shadow clean: zero mismatches, refusals, refused pages, out-of-range accesses,
or reads outside the catalogue. Coverage adds 391 ROM bytes (0.38 KiB; 179
instruction addresses including RAM) beyond the 48 existing movies, notably
`battle_condition_stat_at_least` (`$C1:9130`), `battle_enemy_marches`
(`$C1:3F5A`), and the curtain transition's `$C0:4145`-`$4180` paths. The final
courtroom dialogue also required catalogue entry `strings_f6a000.105`
(`$F6:B1F7`-`$B222`); only its metadata is committed. `ct_native` reports no
stop through frame 47547 but ends blanked after six screen setups; this is
not evidence that it completed the trial (TODO.md, "The native program").
Strict mode consumes all 47547 frames without a halt but ends in the treasury
(`$01A`), still at trial stage `$02` without the fragment; its inputs have
drifted from the reference (81 saved-game bytes differ). Timing calibration with `--drifted`
updated 222 estimates for this narrowed run. Three residual timing exceptions
are documented in TODO.md, with no unlisted video-write movement; the fallback check found
no new or increased fallback. The narrowed suite (including its static checks), with the recording temporarily in the library, ended `suite OK`; the movie was then removed from the library.
### Preparing a Sun Stone cartridge save

`sun_stone_save` is a hand-run route in `HAND_RUN_ROUTES`. It reuses
`son_of_sun` and `sun_stone`: load ctsave13 slot 3, win Son of Sun, take the
Moon Stone, and plant it in 65,000,000 B.C.'s Sun Keep. Then board the Epoch,
travel to the End of Time, choose Disembark in its cockpit, and save to slot 1
at the field's save point. The route exports the game's SRAM as
`<prefix>.srm`; its 26252-frame movie stays outside the library. Replaying
the kept inputs from power-on with the worktree's build produces identical
SRAM. No pokes are used. The resulting cartridge save is
`oracle/saves/routes/sun-stone-planted.srm` (see the saves README).

The placement leaves `$7F:013A` at `$07`, with no Moon Stone in the bag. The
save point's trigger is tile (24, 22); its sprite's anchor is lower than the
tile's centre. The route walks onto the tile, faces left, opens the save
screen, and accepts overwriting slot 1. Its previous slots remain intact.

`sun_stone_finish` (`events-sun-stone.ctm`, 23227 frames) starts from that
save. It boards the Epoch at the End of Time, discovers the empty Sun Keep
in 2300 A.D. (`$0FF`), and visits the empty Keep in 1000 A.D. (`$040`). Porre's
mayor (`$032`, object 8) returns the Moon Stone: the source save already has
his ancestor's Jerky gift, so no 600 A.D. detour is needed. The party replaces
the stone in the present Keep, returns to the future, and Lucca takes the
charged Sun Stone. The scripted return to her house (`$004`) plays Taban
making the Wondershot and Sunshades. The route checks both items, the quest
byte `$F7`, and a successful step on the world map after the scene.

The intermediate quest bytes are `$0F` (theft discovered), `$17` (recovered),
`$37` (replaced), and `$77` (charged stone taken). The Keep's present-day
north shore and Porre's west shore are isolated landing patches; approaches
from the south lead to their entrances. Machine copies find the landing,
walk and conversation, with only successful inputs retained and no pokes.
Shadow replay leaves one Wondershot and one Sunshades in the bag, where the
starting save had neither. The movie adds 283 bytes of executed code against
the preceding 48-movie library, all ported (movies-wanted.md, Target 12).
When boarding from the End of Time, the route recognizes menu mode 8 as the
time dial: the native field-to-menu transition can keep the cockpit's
location `$1D9` while the dial is already taking input.
The route also completes in the shipping mode with the C sound driver (22522
frames), with no fallback or guard refusal; its kept-input replay matches
work RAM exactly. Known entrance walks are tried before a search and retained
only when they reach the requested map, avoiding costly native restores at
each search node. Shadow is clean, and `ct_native` and strict mode both
consume the movie's 23227 frames without a stop; strict's end state drifts
from the reference, as recorded in `tools/strict_baseline.txt`.
The narrowed suite (`--movies events-sun-stone -j 1`, tracing all 23227
frames) ends `suite OK`, including its static checks. The trace agrees in
WRAM, VRAM, SRAM, CGRAM and OAM throughout.

## The wanted list's accounts

What docs/movies-wanted.md said of each target as its movies were recorded: the saves
looked at, what each route plays, and what its shadow, strict and `ct_native` runs showed
against the library of the time. The targets left the wanted list when they were recorded.

### Boss fights (Target 5)
- **Saves**: Guardian at Arris Dome, ctsave02 slot 3 (`$0D6`); R Series in the
  factory, ctsave03 slot 2; Nizbel II in Tyrano Lair, ctsave07 slot 3;
  MotherBrain in Geno Dome, ctsave14 slot 2 (slot 3 is after it).
- **Do**: walk to the boss and win.
- **Reaches**: 1.29 KiB of enemy AI (ported) for the four, plus whatever of the
  animation commands and effects they use; about 0.1 KiB a minute on the AI alone.
- **Route**: yes (walking, `talk()`, the battle bot).
- **Movie**: `battle-guardian` (route `guardian`, ctsave02 slot 3, 25751 frames; cutoff 25552, the
  end kept at the field back in hand): Arris Dome's hall (`$0D6`) through `$0D8` and `$0DD` into the
  Guardian's room (`$0DB`), where confirm beside its object 9 (tile `$08`,`$13`, before the sealed
  door; story bit `$7F:00EC` bit 0 clear) wakes the Guardian and its Bits (formation `$93`); the
  `party` policy won on the 41st try, at frame 23951. It reaches 1.57 KiB no other movie runs,
  1.56 ported: the Guardian's bringing its Bits back (`battle_enemy_bring_back_turn`, `$C1:9E78`,
  and its farthest-enemy search and selector), the battle screen scripts of its and the Bits'
  attacks (`battle_screen_script_center_triangle_on_its_corners`, `..._lay_wedge`,
  `..._wave_lines`, `$CD:3BAB`), and field drawing paths (`$C0:95E3`, `$9619`, `$8405`); 6 bytes
  unported (`$CD:0DFA`). Shadow clean: no mismatch, refusal, `page refused` or `accessor out of
  range`. The route reaches its end in the shipping mode (`route_check --mode on:on:c`: the fight
  won on the 84th try, frame 22954; the end at 24934).
- **Movie**: `battle-r-series` (route `r_series`, ctsave03 slot 2, 65437 frames): the
  Factory Ruins entrance (`$0E4`), upper lab (`$0E5`), lower lab (`$0E6`) and basement
  (`$0EB`). The lab consoles open the ladder and lower security doors; the basement
  code reader checks X, A, B, Y directly, without a warehouse-crane prerequisite.
  Power sets story `$3F`; after Robo holds the closing shutters, the return up the
  lab corridor triggers the six R Series robots and removes Robo from the party.
  Crono and Lucca win with the `r_series` policy (Fire Whirl, Cyclone and the save's
  healing items), without backtracking, at frame 59107. Robo's rescue and repair
  and the Gate trip follow; the movie ends at the End of Time (`$1D0`, story `$48`),
  with the field back in hand, rather than during a location load.
  Its shadow run covers 131499 ROM code bytes. Against the archived shadow
  coverage of the other 48 movies, it adds 910 ROM code bytes (0.89 KiB), all
  under hand-port comparisons, with no new RAM-code
  instruction starts: enemy identity,
  reach and battlefield-half tests (`$C1:8F87`, `$C1:925D`, `$C1:93E6`), learning
  and announcing single/double techs (`rewards_count_tech_points`,
  `rewards_learn_single_tech`, `battle_show_the_rewards`), remembered floor switches,
  held-button event tests and removing Robo from the active party. Shadow clean:
  no mismatches, refusals, `page refused` or `accessor out of range`; sound lockstep
  identical. Strict mode runs all 65437 frames with timing drift in the saved game.
  `ct_native` also runs the full length without a stop, reporting two video escapes
  (first `$7E:5AEB`, the title's plane fill); neither fixed-input native replay is a
  check that the route's outcome survives native timing.
- **Movie**: `battle-motherbrain` (route `motherbrain`, ctsave14 slot 2, 135481 frames):
  from the 600 A.D. world map, board the Epoch and travel to 2300 A.D.; enter Geno Dome
  (`$0F8`), lead with Robo, clear the five conveyor battles (`$07E`) and solve the
  main floor (`$100`). The first and third code switches open the doll's approach;
  carrying the charging pod's electricity to the doll and escort pods sets
  `$7F:013E` bits 2 and 1. The far elevator and upper corridor reach the conveyor
  switch. Escort object 8 must follow around the corridor corners and across that
  belt to the guard at tile `(7,36)`, setting `$7F:013C` bit 7. Both dolls collected
  (bits 1 and 2), Robo beats Atropos alone on `$10C`; the dolls placed on its two
  pedestals open MotherBrain's room. The existing `party` policy wins both bosses
  without a retry in this recording. The route's fresh-start check reaches its
  end at frame 150806, using `policies.motherbrain` to prioritize magic before
  the boss's stat gains; that fight wins without a retry too. MotherBrain's
  defeat sets `$7F:013B` bit 4 and returns the party to the world map; the movie
  re-enters `$0F8` and ends with field control.
  `--coverage` adds 1244 bytes (1.21 KiB) beyond the other 48 movies, all ported:
  351 bytes in `battle_enemy_add_to_stats_then_tech_turn` (`$C1:A20B`),
  `battle_select_weakest_enemy`, the spiral-out screen script, counted object
  movement, the remembered-switch check, and event flag commands. Shadow is clean:
  0 mismatches, no refusals, `page refused` or `accessor out of range`; sound
  lockstep is identical. All four calls of the new boss action are interrupted
  by NMI, so its new instruction coverage is not four completed shadow comparisons.
  Focused fuzzing with the existing constraints (1000 states, seed 1) completes
  868 comparisons of that action with no mismatch; the new tile-row and
  message-pause uncompared entries are recorded in `tools/uncompared_baseline.txt`.
  `ct_native` and strict mode both run all 135481 frames without stopping;
  strict ends with 323 saved-game bytes different from the reference (`drifts`).
  The native host reports four SNES-only video escapes, first at `$7E:5AEB`.
  The narrowed reference fallback check has no new or increased routine, and
  neither reference nor shipping runs have a guard refusal.
- **Movie**: `battle-nizbel-ii` (route `nizbel_ii`, ctsave07 slot 3, 148758 frames): from the
  65,000,000 B.C. world map into Tyrano Lair (`$12B`), through the Reptites' room (`$1AE`) to
  Kino's prison (`$12E`). After the guards, crossing column 55 again brings Kino to the gate;
  confirm from below it starts his rescue. Kino frees the other prisoners on the return through
  `$1AE`, then opens the entrance's other door (`$7F:005C` bit 4). Through the switch room's
  western exit to the upper hall (`$12A`) and the teleport maze (`$131`): follow its warps to
  `(29,47)`, then target the warp to `(15,39)` beside the exit. The lower stairs (`$12E`) lead
  to the hall's middle section and `$12F`, where the right floor switch opens the inner door
  and the wall switch at `(29,5)` sets `$7F:00ED` bit 7, opening Nizbel II's room (`$130`).
  The route fights the hall encounters on both visits, uses a Shelter at the boss room's save
  point, and walks to its far end to start formation `$92` (Nizbel II, 6500 HP). The
  `nizbel_ii` policy uses learned lightning techs, Cure/Kiss, Lapis and Ethers; it wins without
  a retry at frame 146238. The end waits for the victory messages and field control. The story
  bit `$7F:00EE` bit 0 is set before the fight, so the route checks enemy HP and surviving party
  members for victory, not that bit alone. ctsave08 slot 1 has the fight already beaten.
  Against the other 48 movies, it adds 480 instruction starts, 1.05 KiB (1080 bytes), all
  ported: `battle_enemy_add_to_stats_turn` (`$C1:A188`), `battle_condition_stat_at_least`
  (`$C1:9130`), `battle_condition_is_enemy` (`$C1:8F87`), `script_spiral_out` (`$C1:6751`),
  enemy distance keeping, and reward and tech-learning paths. Shadow exits 0 with zero
  mismatches, refusals, `page refused` or `accessor out of range` findings; all 464784
  sound-lockstep ticks are identical. `ct_native` runs all 148758
  frames without stopping, ending blanked (four video escapes, first `$7E:5AEB`). Strict mode
  also reaches the frame limit without stopping, but differs from shadow in 177 saved-game
  bytes (inventory counts and character records among them); its baseline is `drifts`.
  The route also passes from scratch in shipping mode (`route_check --mode on:on:c`): the
  boss falls without a retry at frame 116158, and the field is back in hand at 117868,
  with no refusals. The native-shape route also wins and returns field control (frame 119257).
  Drift-only calibration updates 159 timing costs. `field_after_battle` needs a
  `vblank-tail` timing exception: its shared scalar fits the field returns but overestimates
  the Golem Twins game-over movie's button polling (at most 1173 clocks, less than one
  scanline), which makes no video writes itself and waits for the next frame between walks.
  The added fights cross the uncompared-call reporting thresholds for ten existing entries;
  their baseline explains the enclosing comparisons or interrupts, backed by focused fuzzing
  with the existing constraints (68000 states, zero mismatches). Calibration also moves the
  fixed Terra Mutant movie's strict play to `drifts` (270 saved-game bytes) and exposes one
  already-documented wrong-bank tech lookup in the Spekkio sweep. Its effects-baseline entry
  records the observed read and the preceding zero values that make the scaffolding clears
  redundant in that run. No fallback or refusal baseline changes are needed.

- **Movie**: `battle-castle-slash` (route `castle_slash`, ctsave06 slot 2, 39278 frames;
  cutoff 39128, the defeat flag, then the field back in hand): visit the castle's empty wings,
  return to the hall (`$0A6`) for Ozzie's ambush, take the left passage (`$1D5`) around its
  guards to Slash's room (`$0A9`), defeat the henchmen summoned by object 9, then speak to
  Slash (object 11) and win both the unarmed and sword fights (enemies `$9D` and `$9E`).
  The policy reads learned single techs from the party record, uses Crono's Spincut, attacks
  with Frog, and heals and restores MP with the save's items. Slot 2 has story counter `$87`
  and `$7F:00A3 = $00`; slot 3 is already past both bosses, with `$A3 = $7E` (Slash defeated
  `$04`, Flea defeated `$08`). The route searches machine copies against the destination map
  and position, waiting for requested map changes and treating the entrance's loss of control
  as a scene to finish before walking on. It reaches the field after Slash in the shipping mode
  at frame 40222, with no inexact restores or dropped sound commands.
  Against the preceding 48 movies it adds 860 bytes (0.84 KiB): enemy line-up and marching
  (`$C1:3959`-`$3A3C`, `$3F5A`-`$4020`), keeping distance, farthest-member search and selection
  (`$C1:29FB`, `$A4E0`), and the field's contact handling (`$C0:31B9`-`$326B`). Including
  Flea below, 562 bytes remain unique to Slash, chiefly line-up, marching and farthest-member
  targeting. Shadow clean:
  zero mismatches or refusals, no `page refused` or `accessor out of range`, sound lockstep
  identical. Strict reaches all 39278 frames with saved-game drift; `ct_native`, given the
  cartridge save explicitly, also runs all frames without a reported stop but ends blanked
  after four screen setups, so that run does not establish the fight's outcome (TODO.md).

- **Movie**: `battle-castle-flea` (route `castle_flea`, ctsave06 slot 2, 35256 frames;
  cutoff 35106, `$7F:00A3`'s `$08` defeat bit, then field control): the same empty-wing
  visits and Ozzie ambush, then the right passage (`$0AB`), corridor (`$07D`) and Flea's
  room (`$0AD`). Confirm beside object 11 starts the decoy (`$F0`), which drains Crono's
  MP, then Flea (`$9F`, 4120 HP). Physical attacks, Spincut, Mid Tonics and Ethers win;
  the route uses machine copies for room transitions and checks the defeat flag before
  returning field control. The shipping mode reaches that outcome at frame 40035,
  with no inexact restores or dropped sound commands.
  Against the other 49 movies it adds 62 bytes: status-condition AI
  (`$C1:8F55`-`$8F60`, `$8F6E`-`$8F80`) and an enemy-attack calculation path
  (`$C1:DFB4`-`$DFD2`). Shadow has zero mismatches, no `page refused` or `accessor out
  of range`, and identical sound lockstep. It exposes four `script_face` guard refusals
  at `$C1:5FDE`; the shipping route also encounters that guard. The gap and its 196
  original instructions are recorded in the refusal allowlist, fallback baseline and
  TODO.md's "Refusals in real play". Strict runs all 35256 frames with saved-game drift;
  `ct_native --sram` runs all frames without a reported stop but ends blanked after four
  screen setups, as Slash does. No game logic was ported for either movie; timing was
  calibrated over the two shadow runs with `--drifted`.

### Event commands at the fair (Target 11)
- **Saves**: ctsave11 slot 3, ctsave12, ctsave13, ctsave14 slot 1 (on the 1000 A.D. map, 131 silver
  points at `$7F:0052`; zophar chrono-3 slot 1 80, chrono-2 65, chrono-trigger 50).
- **`$005`** (the lower square): its startup's arithmetic and comparisons (its script's `L03A0` on, as
  `tools/disasm/eventscript.py` decodes it: var from memory, random, subtract and add words, compare
  variables) run only with `$7F:005D` bit 7 set,
  which ctsave14 slot 1 hasn't. The strength test (object `$1A`, tile (5, 34); `move_by_variables`,
  `$9D`) is reached from (89, 586) facing up; pressing confirm there ran only its button test
  (`event_cmd_if_held_a`).
- **`$1B2`** (Bekkler's tent, from `$005` in 67 steps): Bekkler (object `$0B`, function 1) offers
  three games (the script's `L0846`; the clone game at `L0328` for 40 points: compare variables,
  random, shifts; another for 80); no spot the party reached made him answer, so the games weren't
  played.
- **Death Peak** (`$0F4`, from ctsave11 slot 3): recorded in `events-death-peak-wind`
  (4676 frames, route `death_peak_wind`). The earlier 108-frame estimate did not rule out the
  climb: dashing reaches the first shelter, where confirming the guide at tile (55, 48) runs
  `$88` lighten green and turns it into a tree (`$7F:007C` bit 2). Wait still there through a
  gust, then cross at the next calm (`$7F:0218` becomes zero). At the second tree keep pressing
  UP through the gust so the slope does not carry the party out of shelter. Machine copies
  search the departure delays; only the winning crossings enter the movie. See Target 13 for
  the caves and Crono's return.
  which ctsave14 slot 1 hasn't initially; `events-fair-rescue` sets it and plays this startup
  on the return to the square.
- **Movie**: `events-fair-strength` (route `fair_strength`, ctsave14 slot 1, 4276 frames; the
  field back in hand at 4186): face up below object `$1A` (tile (5, 34)), around (89, 584),
  press A to begin, then press A again when local `$14` reaches 21. The first press calls the
  leader's function 3, whose back-and-forth walk measures the hit; the earlier attempt stopped
  at its held-A test. The bell rings, silver points rise from 131 to 132 and the square returns
  to the player. Reaches 87 bytes (0.08 KiB) beyond the previous library and `events-fair-clone`,
  all ported: `event_cmd_move_by_variables` (`$C0:4F0A`-`$4F60`). Shadow clean: 0 mismatched,
  no refusal, `page refused` or `accessor out of range`. Strict mode reaches the end with the
  same saved game and SRAM. `ct_native` reaches all 4276 frames without a stop, though its
  independently timed replay ends with the screen blanked.
- **`$1B2`** (Bekkler's tent, from `$005` at tile (43, 29)): Bekkler's object `$0B`, function 1
  (`L0846`), accepts confirm while local variable `$0B` is zero. Stand around (134, 149), face up
  and press A; wait through his move to tile (8, 5) before the greeting opens. The game choice
  must finish showing before DOWN selects 40 points (`dialog_choice_active`, `$7E:0162`, is 2
  when it accepts input). A short wait after confirm can mistake his movement for no response.
- **Movie**: `events-fair-clone` (route `fair_clone`, ctsave14 slot 1, 8335 frames; the field
  back in hand at 8245): load the save, walk to the fair and the tent, pay 40 of the save's 131
  silver points, copy all 22 poses, receive the clone and return to `$005`. Local `$08` counts
  poses, `$10` selects R/L/A/Y and `$11=$80` opens the answer window. Reaches 167 bytes
  (0.16 KiB) no other movie in the preceding 48-movie library runs, all ported: variable-to-variable
  comparisons (`$C0:6361`-`$63A3`), random shifts (`$C0:658F`-`$65A9`), held-button tests
  (`$C0:66C6`, `$66F9`, `$6705`-`$6718`) and choice-pointer drawing (`$C0:F08B`-`$F0AA`).
  Shadow clean: 0 mismatched, no refusal, `page refused` or `accessor out of range`.
  `ct_native` reaches all 8335 frames without stopping; its timing differs from the movie's.
  Strict mode also reaches the end, but loses the timed game: the clone flag (`$7F:005E` bit 0)
  remains clear, the only differing saved-game byte. Its baseline is `drifts`.
  Against all four fair-game movies, 36 bytes remain exclusive to this one, including the
  held Y/L/R tests (`$C0:66F9`-`$6700`, `$6705`-`$6718`).
- **Movie**: `events-fair-shuffle` (route `fair_shuffle`, ctsave14 slot 1, 6866 frames; the field
  back in hand at 6776): pay 10 points, watch the three soldiers name themselves and shuffle,
  identify the requested soldier (3 in this recording) and win Crono's Poyozo Doll. Local `$0C`
  names the target; objects `$14`-`$16` keep their identities through the permutation chosen by
  `$0E`. This adds no CPU instruction bytes beyond the preceding 48 movies plus the clone and
  strength movies, but covers the unplayed shuffle and reward sequence. Shadow clean: 0
  mismatched, no refusal, `page refused` or `accessor out of range`. Strict reaches all frames
  but loses the game (`$7F:005D` bit 0 stays clear), its only saved-game difference; baseline
  `drifts`. `ct_native` reaches all frames without a stop, ending with its screen blanked.
- **Movie**: `events-fair-rescue` (route `fair_rescue`, ctsave14 slot 1, 7681 frames; the field
  back in hand at 7591): pay 80 points, kick rocks at the three advancing monsters, raise the
  falling companion with the candy four times, clear all three monsters and win the cat. Local
  `$19` records the monsters released; local word `$12` counts the fall, shows the candy at 178
  and loses at 288. The route takes the candy at 190; its call raises the companion and subtracts
  112. Clearing all three monsters after the companion has fallen still loses, so the route
  also checks the cat flag (`$7F:005D` bit 7). Returning to `$005` then runs its cat-care
  arithmetic. Adds 360 bytes (0.35 KiB) beyond the preceding 48 movies, 239 exclusive even with
  the other three fair-game movies: word variable comparisons (`$C0:63A4`-`$63DE`), word
  comparisons (`$C0:64DB`, `$64E7`), variable subtraction (`$C0:6A74`-`$6AA2`), word
  addition and subtraction (`$C0:6AC9`-`$6AF0`, `$6B20`-`$6B4A`) and increment
  (`$C0:6C85`-`$6CA0`), all ported. Shadow clean: 0 mismatched, no refusal, `page refused`
  or `accessor out of range`. Strict reaches the end with matching saved game and SRAM;
  `ct_native` reaches all frames without a stop, ending with its screen blanked.
  The four movies together add 493 executed bytes beyond the preceding 48-movie library.
- **Death Peak** (`$0F4`, a step from ctsave11 slot 3's spot on the 2300 A.D. map; `$7F:007C` is
  3 there, so the object at tile (55, 48) whose confirm runs `$88` lighten green is shown): the wind
  (script variable `$0C`, `$7F:0218`) is calm for the first 108 frames after the party comes in
  and then blows, and a party caught outside the shelter of the tree at tile (55, 49-50) is blown
  back out to the map. The tree is 177 pixels up the slope; dashing up covers about 120 in the calm,
  so no movie.

### The side quests (Target 12)
`events-luccas-mother` plays the campfire and Lara's rescue; the other side quests still need
their events and bosses recorded. The earlier movies from the side-quest saves
`battle-son-of-sun` plays the Sun Stone quest's boss (below). The earlier movies from the side-quest saves
`battle-rust-tyrano` plays Giant's Claw's boss. The earlier movies from the side-quest saves
`events-cyrus` plays Cyrus's side quest. The earlier library's movies from the side-quest saves
`battle-ozzies-fort` plays the fort's events and bosses. The other movies from the side-quest saves
`events-sun-stone` completes the Sun Stone quest (below). The earlier movies from the side-quest saves
(`ctsave12`-`ctsave14`) open menus and shops, ride the Epoch, press the Northern Ruins' switch and
dive into Lavos. The fantasyanime saves step through them in one order (oracle/saves/README.md),
but that order does not guarantee a save before every scene: `ctsave13` already has Lara
rescued, and `ctsave12` has not opened Fiona's desert. Each route must confirm the flags.
`events-fionas-forest` plays Robo's side quest, including Retinite. The other library movies
from the side-quest saves (`ctsave12`-`ctsave14`) open menus and shops, ride the Epoch, press the
Northern Ruins' switch and dive into Lavos. The fantasyanime saves step through them in one order (oracle/saves/README.md),
so each quest has a save just before it; which slot is the nearest, and what of each the library
already runs, is for its route to establish.

The remaining quests' event commands, scene work and bosses are where `ct_native`'s next
stops are likely to be.

- **Movie**: `events-fionas-forest` (route `fionas_forest`, ctsave12 slot 3, 36440 frames).
  The nearest save has `$7F:019E = 0`, `$7F:01A3 = 2` (Retinite unbeaten), and
  `$7F:00F7` bit 1 clear; ctsave13 slot 1 already has the forest restored. The route first
  saves the sapling in the Last Village (`$1A8`) in 12,000 B.C., then enters the Sunken
  Desert (`$09F`/`$0A0`/`$0A1`) in 600 A.D. It wins the central encounter and Retinite
  (field back at frame 26652), leaves Robo with Fiona (`$09E`, frame 29059), crosses
  400 years in the Epoch and recovers him in the shrine (`$039`). The campfire (`$1B4`)
  ends with Lucca controllable in 1000 A.D.; `$7F:019E & 7 = 7`, `$7F:007C` bit 4 and
  `$7F:006D` bit 0 confirm the reunion and campfire. The optional trip to Lucca's mother
  remains a separate target.
  Its shadow coverage is 139886 ROM instruction bytes; 828 bytes (0.81 KiB), all ported,
  are absent from the other 48 movies' coverage. These include the sand's movement and
  party trail paths, `battle_condition_stat_at_least` (`$C1:9130`),
  `event_cmd_send_to_the_reserve` (`$C0:394E`), and event word subtraction (`$C0:6AC9`).
  Shadow is clean: zero mismatches, no `page refused`, no `accessor out of range`, and
  no uncatalogued asset reads. The full generated-only replay matches the reference trace.
  The departure's two guards refuse 60 calls each, recorded as gaps in `tools/refusal_allowlist.txt` and TODO's
  "Refusals in real play"; they are not shadow mismatches.
  `ct_native` runs all 36440 frames without a stop (four video escapes, first `$7E:5AEB`,
  the existing title-screen escape in TODO). Strict mode also reaches the last frame,
  but timing moves the inputs: 131 saved-game bytes differ (inventory counts and character
  records among them), recorded as `drifts` in `tools/strict_baseline.txt`.
  The narrowed suite (`--movies events-fionas-forest -j 1`) ends with `suite OK`, including
  its static checks; the calibrated timing exceptions are documented in `tools/timing_exceptions.txt`.

- **Movie**: `events-luccas-mother` (route `luccas_mother`, 15701 frames,
  about 4 minutes 22 seconds). From the prepared cartridge save, recover Robo
  at Fiona's shrine (`$039`), hear the campfire conversation (`$1B4`), follow
  Lucca through the red gate (`$053`) to her bedroom in 990 A.D. (`$00A`), and
  enter the workshop (`$004`) through the front room (`$003`). When Lara is
  caught, reach the console and enter L, A, R, A. Copies of the console state
  let the route retry: three-frame taps with 8- or 16-frame gaps fail;
  eight-frame holds with 24-frame gaps, after four idle frames, succeed.
  `$7F:007C` bit 7 proves Lara was saved, rather than merely that the prompt
  closed. Return to Robo, receive the GreenDream (`$AA`) and end with control
  on the 1000 A.D. world map, proved by a movement probe on a copy.
- **Save**: `prepare_luccas_mother` starts from technoplaza `ct-state11`
  slot 1, whose seed is planted (`$7F:00F7 = $0A`) and Retinite is unbeaten
  (`$7F:01A3` bit 0 clear). It defeats Retinite, leaves Robo restoring the
  forest, flies to 1000 A.D. and saves through the menu. The library starts
  there (`$7F:019E = $F9`, `$7F:007C = $0E`); the long prerequisite is a
  `HAND_RUN_ROUTES` entry, 40031 frames, with its movie kept outside the library.
  Replaying the preparation reproduces the saved SRAM byte for byte. The save's
  provenance is in [oracle/saves/README.md](../oracle/saves/README.md#routes).
- **Coverage**: 60.88 KiB of instructions executed (58.49 KiB in ROM);
  150 bytes (0.15 KiB)
  no other movie in the preceding 48-movie library executes, all ported.
  These include the held A/L/R event commands (`$C0:66C6`, `$6705`, `$670F`),
  sending a member to reserve (`$C0:3967`-`$3993`, `$39E1`-`$39F3`), taking
  formation (`$C0:3346`-`$334F`), field drawing and walking paths.
- **Checks**: shadow clean, zero mismatches, guard refusals, `page refused`
  or `accessor out of range`; sound lockstep identical. The last dialogue
  exposed the omitted final entry of the `$FF:4460` string table; its count
  is now 81, with `strings_ff4460.80` catalogued at `$FF:582A`-`$583C`.
  Strict mode completes and matches the reference saved game and SRAM.
  The shipping run exposes the existing row-tail alias into object 0's
  in-view flag (`field_update_objects`, `$7E:0F00`, 984 reads from frame
  12470). It is recorded with its cause as a gap in the effects baseline
  and TODO's "What only the original's memory has, past the object tables";
  native-shape completion does not establish identical intermediate drawing.
  `ct_native` stops at frame 3120 on Robo's recruitment (`$C0:38BE`,
  `event_cmd_recruit`); see TODO's "The native program". Timing is calibrated
  for the narrowed run with `--drifted`; the existing fallback baseline
  already covers this movie's original-code paths.
  The narrowed suite (`--movies events-luccas-mother -j 1`), including its
  static checks, ends with `suite OK`.

- **Movie**: `battle-son-of-sun` (route `son_of_sun`, ctsave13 slot 3, 16377 frames):
  board the winged Epoch on the starting tile outside Guardia Castle, turn the dial to 2300 A.D.,
  land east of the Sun Palace (`$0FB`), enter and approach Son of Sun. Crono, Lucca and Marle win
  without a wipe; the movie ends with the palace field back in hand. `$7F:013A` is `$00` in all
  three ctsave13 slots and `$F7` in ctsave14 slot 1, confirming the last pre-quest save. The
  palace sets bit 0 before the fight, and picking up the Moon Stone sets bit 1 afterwards.
  Enemy `$F7`'s AI, decoded with `tools/disasm/battlescript.py`, records Roulette Shuffle in
  stat offset `$3C`: `$62` is the real flame, `$63` counters. The policy reads it each turn,
  attacks and heals with items. Enemy `$F6` ends the fight at 10000 HP or less, from 12000;
  the surviving flames make an all-enemies-dead test inappropriate.
  It reaches 183 bytes (0.18 KiB) no other movie in the preceding 48-movie library runs:
  `battle_select_other_enemy_at_random` (`$C1:ABC9`-`$AC13`), `battle_select_other_enemies`
  (`$C1:A6C3`-`$A6EC`), `$C1:E748`-`$E75E`, `$C1:AF0D`-`$AF1C`, and short reward and Epoch
  branches. Shadow: zero mismatches, no guard refusal, `page refused` or `accessor out of range`.
  `ct_native` and strict mode reach the movie's last frame without a stop; strict mode drifts
  from the reference's saved state, as recorded in `tools/strict_baseline.txt`.
  The route wins in the shipping mode with the C sound driver at frame 27579, no wipe,
  fallback or guard refusal; replaying its kept input reaches identical work RAM. The final
  interpreter route reproduces the movie byte for byte. Timing: 227 estimates calibrated
  with `--drifted` over this narrowed run, with the field NMI's upload mix and the scripted
  defeat's seeing-off work recorded in `tools/timing_exceptions.txt`; no game logic ported.
  The narrowed suite (`--movies battle-son-of-sun -j 1`), including its static checks, ends
  `suite OK`.
- **Hand-run extension**: `sun_stone`, 22857 frames from the same save, repeats the fight,
  climbs to the Moon Stone (item `$DE`, quest byte `$7F:013A` becomes `$03`), returns to the
  Epoch and visits the Sun Keep in 65,000,000 B.C. (`$147`). Talking to object 8 and accepting
  the placement removes the Moon Stone and sets bit 2: quest byte `$07`, field back in hand.
  Machine copies search for a landing; no pokes. The movie stays outside the library and
  `HAND_RUN_ROUTES` excludes the extension from the long-route gate. The stone is planted in
  prehistory, not 2300 A.D.; the latter's Keep (`$0FF`) is its eventual collection point.
  **Still unrecorded**: the theft discovered in the later Keep, the Porre mayor's recovery
  chain, replacing the stone, and Lucca collecting the charged Sun Stone. The field scripts
  distinguish these in the remaining bits of `$7F:013A`; the boss movie deliberately ends
  before the pickup so it stays a short battle regression.

The remaining quests are event-heavy plays the native program hasn't met: the event commands, scene work and

- **Rainbow Shell trial, hand-run route**: `rainbow_shell_trial`, ctsave13 slot 2,
  47547 frames, ending with control in Guardia Castle (`$015`). The slot has
  `$7F:00A1` bit 6 set (shell delivered), `$7F:00A2` bit 7 clear (fragment not
  collected), `$7F:0050` bit 6 clear (King not exonerated), and trial stage
  `$7F:0069 = $00`; slot 3 already has the fragment and exoneration flags.
  The route plays the first hearing, treasury fights, the fragment and courtroom
  interludes, the Chancellor exposed as Yakra XIII, the fight (won at frame
  41423), and Marle reunited with her father. It ends with the exoneration bit set
  and stage zero. The recording stays outside the library under `HAND_RUN_ROUTES`.
  The first successful play took 48156 frames; faster battle/message settings
  and dialogue confirmation reduced it to 47547, still too long. Preparing a
  nearer save was attempted from the evidence-collected stage `$10`: the party
  returned down the court's stairs to `$015`, but neither the exit search nor
  the attempted walks reached the world map. No edited save or machine state
  is supplied. A shorter approach or a normal cartridge save before the final
  hearing remains necessary for `events-rainbow-shell-trial.ctm`.
  Shadow clean: zero mismatches, refusals, refused pages, out-of-range accesses,
  or reads outside the catalogue. Coverage adds 391 ROM bytes (0.38 KiB; 179
  instruction addresses including RAM) beyond the 48 existing movies, notably
  `battle_condition_stat_at_least` (`$C1:9130`), `battle_enemy_marches`
  (`$C1:3F5A`), and the curtain transition's `$C0:4145`-`$4180` paths. The final
  courtroom dialogue also required catalogue entry `strings_f6a000.105`
  (`$F6:B1F7`-`$B222`); only its metadata is committed. `ct_native` reports no
  stop through frame 47547 but ends blanked after six screen setups; this is
  not evidence that it completed the trial (TODO.md, "The native program").
  Strict mode consumes all 47547 frames without a halt but ends in the treasury
  (`$01A`), still at trial stage `$02` without the fragment; its inputs have
  drifted from the reference (81 saved-game bytes differ). Timing calibration with `--drifted`
  updated 222 estimates for this narrowed run. Three residual timing exceptions
  are documented in TODO.md, with no unlisted video-write movement; the fallback check found
  no new or increased fallback. The narrowed suite (including its static checks), with the recording temporarily in the library, ended `suite OK`; the movie was then removed from the library.

Each is an event-heavy play the native program hasn't met: the event commands, scene work and
bosses they reach are where `ct_native`'s next stops are likely to be.
These event-heavy plays exercise event commands, scene work and bosses that
the earlier library did not reach, and may expose `ct_native`'s next stops.

- **Movie**: `events-cyrus` (route `cyrus`, ctsave14 slot 1, 39872 frames):
  the carpenter's request in Choras, 600 A.D.; his counterpart's loan and
  the tools collected from the family in 1000 A.D.; the tools delivered,
  the first repair, all three guardian groups in the ruins' left wing
  (`$042`), and the carpenter's 2000 G repair of that floor. Frog replaces
  Crono, reaches the grave (`$049`), speaks with Cyrus's ghost, and receives
  the restored Masamune from Masa and Mune. The field is back in hand at
  the end. Slot 1 has `$7F:019F = $00`, `$7F:01A3` bit 6 clear and Frog's
  weapon `$7E:2769 = $3D`; slot 2 has `$EE`, the completion bit set, and
  weapon `$42`. The movie ends with repair flags `$26`, the completion
  bit set and weapon `$42`: only the left wing needs clearing and the
  paid repair; the right wing's treasure rooms are outside this play.
  It reaches 0.29 KiB (298 bytes) no other movie runs, all ported, measured
  against the other 48 movies' cached coverage: the overworld's region-song
  fade and restart (`$C2:3011`-`$309D`, with gaps), the event's item given
  from a variable and item removal (`$C0:37C7`-`$37F8`), and taking the last
  inventory item (`$C1:D056`-`$D085`, with gaps); smaller field, battle and
  overworld paths account for the rest. Shadow clean: 0 mismatches, no
  guard refusals, `page refused` or `accessor out of range`; the C sound
  driver matches all 122254 ticks. The route also reaches the restored sword
  and field control in shipping mode (`--native on --generated on
  --sound-driver c`), at frame 38104. Strict mode (`--native on --generated off
  --no-interpreter strict --sound-driver c`) reaches all 39872 frames, with
  five saved-game bytes differing in `character_records` (first addresses
  `$7E:2607`, `$7E:26A3`, `$7E:26A4`, `$7E:26A7`), recorded as `drifts` in
  the strict baseline. `ct_native` reports no stop through all 39872 frames,
  but ends blanked after 12 screen setups, with two SNES-only video escapes
  (first `$7E:5AEB`); this does not establish quest completion in that
  frontend (TODO.md, "The native program").
  Drift-only calibration updates 217 registry estimates. Eight entries
  retain measured timing exceptions (ten limit lines), including pose
  timing attribution that cannot be fitted with a positive remaining rate;
  TODO.md records the follow-up. The narrowed vblank check has no new findings.

- **Movie**: `battle-rust-tyrano` (route `rust_tyrano`, `rainbow-shell.srm`
  slot 1, 23282 frames): from Giant's Claw's save room (`$06D`), open the
  door and enter the shell chamber (`$0C5`). Crono, Lucca and Robo defeat
  Rust Tyrano at frame 21752 using Confuse, Uzzi Punch, healing and MP
  restoration; the movie includes the countdown and fire breath and ends
  once field control returns. A fresh route run reproduced the recording.
  Coverage adds 26 instruction addresses, 54 bytes (0.05 KiB), over the
  preceding 48 library movies, all in existing ports: walking's ground and
  collision branches (`$C0:8AA9`, `$C0:9AB0`), healing calculation tails
  (`$C1:DAE3`, `$C1:DB04`), reward HP/MP growth and experience
  (`$C1:F8FC`, `$C1:F916`). Shadow clean: zero mismatches, refusals,
  `page refused` or `accessor out of range`.
  `ct_native` and strict mode both run all 23282 frames without stopping;
  strict ends with 72 bytes of inventory/character-record differences
  (`drifts`). The native host reports four existing video escapes, first
  `$7E:5AEB`.
  The shipping-mode route (`route_check --mode on:on:c`) wins at frame
  23105 and returns field control at 24605 with no backtracking and zero
  fallback instructions or guard refusals, using Life 2 to recover a fallen
  member. The reference movie needs no revival.
- **Save and approach**: the archive's slot 2 is not midway through the
  dungeon. In slots 1/2/3, `$7F:01D2` is `$04/$7C/$7C`, `$7F:00A9` is
  `$1E/$9E/$9E`, and `$7F:00A1` is `$23/$63/$63`: the boss encounter,
  departure with the shell and the 600 A.D. castle delivery are already
  flagged in slot 2. The trial changes `$7F:00A2` from `$18` to `$B8` and
  `$7F:00A7` from `$00` to `$40` between slots 2 and 3. The `rainbow_shell`
  hand-run route starts at slot 1, visits the throne, clears the cave
  encounters and switches, uses a Shelter and saves normally at Giant's
  Claw's save point. That 55184-frame approach stays outside the library;
  its cartridge save supplies the short boss movie. No memory pokes.
  Replaying those winning inputs from the archive save recreates all 8192
  bytes of the committed SRAM exactly.
- **Still wanted**: the shell's retrieval and delivery, then Marle's
  father's trial in 1000 A.D. Start the latter from ctsave13 slot 2, where
  delivery is complete and the trial flags are clear. This is a separate
  route, not a cheap extension of the boss movie.

- **Movie**: `battle-ozzies-fort` (route `ozzies_fort`, ctsave12 slot 2, 39237 frames):
  from the End of Time by Epoch to 600 A.D.; `$0B1`'s introduction, Flea Plus in `$0B7`,
  `$0B2`'s conveyor, Super Slash in `$0B8`, `$0B3`'s treasure trap (the chest left alone),
  the three generals in `$0B4`, then `$0B5`'s switch battle, the fall back to `$0B4`,
  and the return for the cat to spring Ozzie's final trap. Slot 2 has `$7F:01A1 = $00`;
  slot 3 has `$FF` and `$7F:01A0` bit 7 set. The movie ends with those fort flags set
  and the field back in hand. The trio's counters make weak attacks expensive: Lucca's
  Flare, with Crono and Marle healing, wins without a wipe and keeps the whole fort
  below 40000 frames. Machine-copy searches retain only the winning inputs; no pokes.
  Its `--coverage` adds 393 bytes (0.38 KiB) beyond the preceding 48 movies, all ported:
  `event_cmd_set_flags` (`$C0:6BF7`), single-tech learning (`rewards_count_tech_points`,
  `rewards_reach_next_tech`, `rewards_learn_single_tech`, `$C1:F3AF`-`$F532`), the
  rewards' messages (`$FD:ADC9`-`$AE04`), the choice pointer (`$C0:F08B`-`$F0AA`), and
  the Epoch's arrival (`$C6:EE31`), with small status, level-growth and text paths.
  Shadow: 0 mismatched, no refusal, `page refused` or `accessor out of range`; sound
  state identical. Strict mode reaches the full frame budget but the saved game
  drifts. `ct_native` reports no stop, reaches 39237 frames, and leaves its display
  blanked after five screen setups; completion there is not established (TODO.md).

- **Sun Stone preparation**: the hand-run `sun_stone_save` reuses the Son of Sun fight and
  Moon Stone placement routes, then writes slot 1 at the End of Time's save point (26252
  frames). Its movie stays outside the library; `oracle/saves/routes/sun-stone-planted.srm`
  is the game's own cartridge save, reproduced byte for byte by replaying the kept inputs.
  The stone is planted (`$7F:013A = $07`), and the source ctsave13 slot 3 already has the
  Jerky gift to the mayor's ancestor. The theft, recovery and rewards start from this
  save without repeating the boss. No machine writes or edited SRAM.
- **Movie**: `events-sun-stone` (route `sun_stone_finish`, `sun-stone-planted` slot 1,
  23227 frames): board the Epoch from the End of Time, visit 2300 A.D.'s empty Sun Keep
  (`$0FF`, theft discovered at frame 6398), then its 1000 A.D. counterpart (`$040`).
  Porre's generous mayor (`$032`, object 8) returns the Moon Stone at frame 13063.
  Replace it in the present Keep (16762), return to the future, and have Lucca collect
  the charged stone. The return to her house (`$004`) and Taban making the Wondershot
  and Sunshades play through; end after an ordinary step on the world map. The route
  checks quest byte `$7F:013A` through `$07`, `$0F`, `$17`, `$37`, `$77`, `$F7`, and both
  rewards in the bag, where the starting save had neither. The prior Jerky gift is
  already in the source save (`$7F:01D2` bit 2); this movie does not replay that gift.
  It reaches 283 bytes (0.28 KiB; 135 instruction starts) no movie in the preceding
  48-movie library runs, all ported: `event_cmd_take_item` (`$C0:37E3`-`$37F8`) and
  `battle_inventory_take` (`$C1:D056`-`$D085`), the event leader and flag commands
  (`$C0:658F`-`$65A9`, `$6B20`-`$6B4A`, `$6BF7`-`$6C1B`), the choice pointer's
  `$C0:F08B`-`$F0AA`, overworld followers (`$C2:3D0A`-`$3D29`), and short Epoch
  arrival/reach branches including `$C6:EE31`-`$EE37`.
  Shadow: zero mismatches, no guard refusal, `page refused` or `accessor out of range`;
  sound lockstep identical. The reference end state has `$F7` and both rewards.
  `ct_native` and strict mode consume all 23227 frames without stopping. Strict mode's
  saved state drifts in 23 bytes (inventory, party waiting, song and field state),
  recorded in `tools/strict_baseline.txt`; fixed inputs do not follow shipping timing.
  The state-driven route completes in the shipping mode with the C sound driver at
  frame 22522: both rewards, no fallback or guard refusal, all 429 restores away from
  a native base replayed exactly, and the kept inputs replay to identical work RAM.
  It recognizes the Epoch dial by menu mode as well as location, and tries verified
  entrance walks before searching so native restores do not dominate each entrance.
  Timing: 141 estimates recalibrated with `--drifted` over this narrowed run; the
  field NMI's varied upload queues are recorded in `tools/timing_exceptions.txt`.
  The narrowed suite (`--movies events-sun-stone -j 1`) ends `suite OK`,
  including the static checks and a full 23227-frame trace comparison of WRAM,
  VRAM, SRAM, CGRAM and OAM with no divergence.

### The story events (Target 13)

- **North Cape, 12,000 B.C.**: `events-magus-joins` (route `magus_joins`,
  ctsave11 slot 2, 13663 frames): leave the Earthbound Village (`$1A9`) for
  the post-flood world map (`$1F6`), enter North Cape (`$1AC`), examine the
  glimmer (object `$10` at tile (7, 6)), and play Magus's scene and flashback.
  Refuse the fight, walk south to his offer, accept the default name and
  close the party screen; end with the field back in hand and `$7F:0138`
  bit 0 set. The starting flags confirm the nearest save: story progress
  `$7F:0000 = $D2`, `$7F:0138 = 0`; ctsave11 slot 3 and every ctsave12 slot
  already have Magus recruited. Marle, Lucca and Robo are the active party.
  Against the preceding 48 movies it reaches 0.08 KiB (83 bytes) no other
  movie runs, all ported: flags selected by number (`$C0:6B20`-`$6B4A`),
  screen tint (`$C0:3D50`-`$3D5E`), and small field/dialog/palette branches.
  Shadow has 0 mismatches, no `page refused` or `accessor out of range`;
  164 guard refusals in the flashback are the hidden-leader gap recorded
  in TODO.md and `tools/refusal_allowlist.txt`. `ct_native` stops at frame
  8531 at `$C0:881E` ("a walk the port doesn't take"); strict mode stops at
  frame 8561 at the same entry, before the fight/refuse choice.
- **North Cape's party fight**: `battle-magus-north-cape` (route `north_cape`,
  the same ctsave11 slot 2, 28653 frames) shares the scene, then accepts the
  fight. Marle, Lucca and Robo defeat Magus (6666 HP); Frog is absent, so
  this plays the party battle. The `party` policy wins at frame 26953 in
  31 decisions with no retry. Close the aftermath and end with the field
  back in hand and `$7F:0138` bit 1 set. Its new coverage against the
  preceding 48 movies is 31 bytes, all ported: eight in the rewards' HP/MP
  growth path (`$C1:F8FC`-`$F903`) are exclusive to this movie, and 23 are
  shared with recruitment. Recruitment retains 60 exclusive bytes; together
  they add 91 bytes. Shadow has 0 mismatches, no `page refused`, `accessor
  out of range` or uncatalogued asset reads. Its 164 flashback refusals and
  `ct_native`/strict stops are the same as recruitment's above.
  The narrowed two-movie suite with `-j 1`, including its static checks,
  ends `suite OK`; both movies and both standard routes also pass with
  scaffolding compiled out. The pair's timing costs were calibrated with
  `--drifted`, with the remaining timing tails documented as exceptions.

The library plays the fair, the prison escape, Arris Dome, the race, the Magic Cave, Magus's
castle, the dactyls, the Black Tyrano, the Keeper's Dome, the Golem Twins, the Blackbird, North Cape, the
The library plays the fair, Crono's trial, the prison escape, Arris Dome, the race, the Magic Cave, Magus's
castle, the dactyls, the Black Tyrano, the Keeper's Dome, the Golem Twins, the Blackbird, the
Black Omen and Lavos. The rest of the story has saves just before it (oracle/saves/README.md) and
no movie; the slot named is the nearest found from the README, for the route to confirm.

The save flags narrow two of these targets: ctsave03 slot 1 has story counter
`$7F:0000 = $37`, before Proto Dome's repair sets `$3C`; slot 2 is already at
`$3C` with Robo in the party. ctsave11 slot 1 is at story `$CF`, with Blackbird
stage `$7F:00D2 = $00` and the Epoch's wings bit (`$7F:00BA` bit 7) clear;
slot 2 is already at story `$D2`, Blackbird stage `$0A`, and wings acquired.
`events-proto-dome.ctm` now plays Robo's repair from ctsave03 slot 1 in 24717
frames: the Lab 32 race approach, both Proto Dome encounters, Lucca repairing
Robo, his introduction and naming, and the party choice. It ends with control
in the dome, story `$3C`, repair stage `$0F`, and Crono/Robo/Lucca in the party.
Shadow clean (zero mismatches, guard refusals, refused pages, out-of-range
accesses, or missing catalogue entries). Its coverage adds 147 ROM bytes and
71 instruction addresses including RAM beyond the original 48 movies, chiefly
the enemy AI's other-kind and enemy-identity conditions. No new or increased
fallback. `ct_native` runs all frames but ends blanked after four screen setups;
the repair's completion is not established. Strict mode consumes every frame without a halt but ends in Proto Dome at
story `$37`, repair stage `$00`, with Robo still absent (59 saved-game bytes
differ).
The 60 interrupted tile-row decoder calls (of 102) were checked separately:
5000 differential-fuzz cases with the existing constraints all matched, none
skipped; the other 42 movie calls matched in shadow. Drift-only timing
calibration updated 204 estimates; four residual exceptions are documented in
TODO.md, with no unlisted video-write movement. The narrowed suite, including its static checks, ended `suite OK`.
`battle-dalton.ctm` now plays the Blackbird's end in 39666 frames. Its
`blackbird-before-dalton` slot 1 is a normal cartridge save prepared from
ctsave11 slot 1 after `events-blackbird`'s opening, all three equipment
chests, and the inventory chest. The 74786-frame `blackbird_save` preparation
is a hand-run route, with its movie outside the library. The new movie
starts in the cell at story `$CF`, stage zero, with equipment bits
`$7F:00AF & $07 = $07`, inventory bit `$7F:00BA & $02` set, and wings clear.
It crosses the ducts and wing, defeats Golem Boss (frame 21922), plays the
Epoch reveal, defeats Dalton Plus (33954), tests the Epoch's controls, and
destroys the Blackbird. The landing conversation ends with walking control
on `$1F6`, stage `$0A` and wings set. See MOVIES.md and the saves README for
the route and save provenance.

Shadow reports zero mismatches, refused pages, out-of-range accesses, or
uncatalogued reads. Its 20 walking-guard refusals during the jump onto the
Epoch are a gap in refusal_allowlist.txt and TODO.md, with five new fallback
rows and no other new or increased fallback. Coverage adds 392 ROM bytes
(201 instruction addresses including RAM) beyond the other 49 library
movies: `event_cmd_set_flags`, in-view conditions, direction-to-target, and
layer-scroll paths. The `$FF:6B00` dialogue table and its 185 strings were
added to the catalogue as metadata. `ct_native` consumes all frames with no
explicit stop but only three screen setups; completion is not established.
Strict mode halts at frame 25171 at `$C0:881E`, before Dalton Plus. Drift-only
calibration updates 226 estimates; the residual animation-scheduler timing
exception is documented in TODO.md, with no unlisted video-write movement.
The mostly abandoned scrolling-command comparisons were checked separately
with existing fuzz constraints: 2999 matches, zero mismatches, 46875 guard
skips and 126 call-out budget skips in 50000 cases.
The narrowed suite, including its static checks, ended `suite OK`.

- **Movies, the Ocean Palace**: `battle-golem-twins` (route `golem_twins`, ctsave10
  slot 3, 20216 frames) wins with alternating Lightning 2, Ice 2 and Fire 2
  (frame 15912, no retries), watches Dalton leave, returns to the save point,
  saves slot 3 and closes the menus. It adds no unique instruction bytes, but
  supplies the reproducible cartridge save `recorded/ocean-palace-after-twins.srm`.
  `events-ocean-palace-lavos` (route `ocean_palace_lavos`, that save's slot 3,
  30364 frames) passes `$19E` to `$19F`, speaks to the Nu in the corridor,
  approaches the Mammon Machine, loses the scripted Lavos fight (`$1D3`), walks
  Crono forward at `$1A7`, and plays his fall, Schala's escape and Zeal's
  destruction (`$1F4`). It reaches the Last Village hut (`$1A9`) at 26248;
  after waking and closing the party exchange, the field is in hand at 30364,
  story counter `$CD`. This adds 1037 bytes (1.01 KiB) beyond the other 49 movies,
  statically credited to ports: the capsule iris (`$C6:E448`-`$E567`), world-map
  script work (`$C6:E595`-`$E6CF`, `$C2:16FA`-`$1758`, `$C2:191F`-`$1946`,
  `$C2:7BD1`), event movement/item removal and field drawing. Four real-play
  guard gaps remain (TODO.md, "Refusals in real play"). The newly read dialogue
  tables `$F9:B000` and `$FF:6B00` and their 443 and 185 strings have catalogue
  metadata, not asset bytes.
  Both shadow runs have zero mismatches, page refusals and out-of-range
  accessors. Strict mode reaches both ends with saved-game drift (58 bytes
  each, plus 224 SRAM bytes for the Twins), recorded in the strict baseline.
  `ct_native` reaches both frame counts (exit 0) but reports four/two unsupported
  video calls, first at `$7E:5AEB`; the finale ends blanked, a playback gap
  recorded in TODO.md, "The native program". The reference movie reaches the
  requested ending; native playback still needs that follow-up.
  The narrowed one-job suite for these two movies ends with `suite OK`,
  including static checks, native-shape playback and identical sound lockstep.
- **Yakra hand run**: `battle-yakra` (route `yakra`, ctsave01 slot 2,
  71287 frames, in `oracle/movies/hand/`, outside the default library). The
  slot is on the 600 A.D. world map (`$1F1`), story `$12`, before the pin and
  Frog's arrival, rather than at the boss door. It plays the Cathedral
  (`$081`), the Naga-ettes and Frog's naming, the entrance organ, the inner
  hall (`$082`), the switches and inner organ (`$083`), the guarded corridor
  (`$084`), and Yakra (`$0C6`, enemy `$90`, 920 HP). Yakra falls at frame
  69207; the rescue scene ends with field control at 71287 and `$7F:01ED = 1`.
  Story `$1B` is set by the later return-to-castle interaction, beyond this
  movie's endpoint. The exploratory route was 81704 frames; choosing faster
  settings, using learned early techs and taking the right staircase first
  shortened it, but no route near 40000 frames was found. It remains in
  `HAND_RUN_ROUTES`; a library-length recording of this boss is still wanted.
  Only the winning inputs are kept; no diagnostic writes were used.
  It reaches 491 instruction addresses (1036 instruction bytes) absent from
  the previous 48 movies, including `script_spiral_out` (`$C1:6751`),
  `battle_condition_other_kind_present` (`$C1:9656`), and tech-learning rewards
  (`$C1:F39A`, `$FD:AD17`). The first asset-backed replay exposed the already
  described but uncatalogued `$F9:B000` dialogue table; its 444 extracted
  resources are now in the catalogue. `ct_native` consequently passes its
  original frame-4032 stop at `$F9:B006` and reaches the frame limit, reporting
  a blanked screen. Strict mode also reaches the limit, with 128 saved-game
  bytes differing from the reference; its baseline is `drifts`. Neither
  result is a claim that the fixed inputs finish the boss on native timing.
  The rebuilt shadow run is clean: zero mismatches, guard refusals, page
  refusals, out-of-range accessors or uncatalogued reads; sound lockstep is
  identical over 220210 ticks. The fallback ratchet needs no new baseline
  rows. The narrow timing calibration changes scalars only; its remaining
  data-dependent timing tails are documented in `timing_exceptions.txt`,
  with no unallowed finding in the shipping-mode video-write check.
- **Dragon Tank movie**: `battle-dragon-tank` (route `dragon_tank`, ctsave02
  slot 1, 16042 frames). The save is in `$01D`, story `$2E`, with `$7F:0199`
  bit 2 clear. The route enters the bridge (`$01C`), continues left after
  the arrival scene returns control, and defeats the head (600 HP) before
  the body (266 HP) and wheels (208 HP), using physical attacks and the
  available Tonics. The fight ends at 14722; after its destruction scene,
  the field is back in hand at 16042 with `$7F:0199` bit 2 set. It reaches
  290 instruction addresses (631 bytes) absent from the previous 48 movies
  and the Yakra hand run: large field sprite paths in
  `field_draw_object_sprites` (`$C0:B5CB` onward) and
  `field_rebuild_12_sprites` (`$C0:C820` onward), plus
  `battle_setup_graphics` and `battle_copy_frame_tiles` (`$CF:EBEB` onward).
  Shadow is clean: zero mismatches, guard/page refusals, out-of-range
  accessors or uncatalogued reads; sound lockstep is identical over 49989
  ticks. No fallback baseline increase is needed. `ct_native` reaches 16042
  with the screen showing; strict also reaches the limit, differing from
  the reference by 35 saved-game bytes (`drifts`). Neither has a code stop.
  The combined effects check exposed the grid's party-off-grid traversal:
  it scans slots 3 through 13, so object 23's Y aliases the drawn frame's X
  at `$7E:1D2E`. The existing frame-X store now stays outside
  `ORIGINAL_EFFECT`, since the grid consumes it as game state. This removes
  the scratch reads attributed to `battle_dispatch_expired_timers` and
  `battle_carry_out_member_action` without adding a new port.

- **Zombor movie**: `battle-zombor` (route `zombor`, ctsave04 slot 1,
  26320 frames). The save starts outside Zenan Bridge at story `$54`, with
  Jerky in the inventory. Delivering it sets `$57`; agreeing to defend the
  bridge sets `$7F:019A` bit 0. The movie defeats both troop encounters,
  then Zombor's 800-HP lower and 960-HP upper bodies. The fight ends at
  25420; field control returns in `$087` at 26320, story `$5A` (which the
  encounter sets before combat, so the route also checks the victory and
  field control). Against the previous 48 movies plus Yakra and Dragon
  Tank, it adds 172 instruction addresses / 369 bytes: notably
  `battle_enemy_marches` (`$C1:3F5A`), `battle_inventory_take`
  (`$C1:D056`), and event facing/flag commands. Shadow has zero mismatches,
  refusals, out-of-range accessors or uncatalogued reads; sound lockstep
  matches for 81810 ticks. No fallback baseline increase is needed.
  `ct_native` reaches the limit with its screen blanked; strict stops at
  20301 on the `$C0:1AAC` `field_press_confirm` guard. These are TODO gaps,
  with the guard recorded in the refusal allowlist. The shipping replay
  refuses at that same frame and state, then uses the original code.

- **Nizbel hand run**: `battle-nizbel` (route `nizbel`, ctsave05 slot 2,
  57673 frames). The save is outside the Reptite Lair (`$1F3`, story `$7B`),
  with Crono, Ayla and Marle. The route enters `$11B`, takes the open hole
  in `$11C`, clears the enemies guarding holes in `$11D` and `$11E`, then
  crosses `$11F` and `$120` to Nizbel's chamber (`$121`). A constrained
  search on the bottom floor shortened the approach, but no complete play
  under 40000 frames was found. It remains outside the default library in
  `oracle/movies/hand/`, and `nizbel` is in `HAND_RUN_ROUTES`.
  Lightning lowers Nizbel's defense; physical attacks, Aura, Tonics and
  Ethers finish the fight at 54768. The post-fight scene sets story `$7C`
  and returns the Gate Key; the return to Ioka and Ayla's farewell set `$7D`.
  At 57673 the field is back in hand in `$113`, with the Gate Key in the bag.
  It adds 172 instruction addresses / 385 bytes beyond the previous 48
  movies and the other three early-boss recordings: variable comparisons
  (`$C0:6361`), `battle_condition_is_enemy` (`$C1:8F87`) and enemy state
  changes at `$C1:9CB3`, among others. The first shadow run had zero
  mismatches but reported an uncatalogued read at `$D8:DE9C`. The existing
  `$D8:DD80` dialogue table was already extracted; adding its 256 missing
  catalogue entries supplies metadata, without adding cartridge bytes or
  porting game logic. The rebuilt shadow run is clean: zero mismatches,
  guard/page refusals, out-of-range accessors or uncatalogued reads.
  Sound lockstep matches for 178633 ticks. No fallback
  baseline increase is needed. `ct_native` reaches the limit with a blanked
  screen; strict reaches it with 128 saved-game bytes different (`drifts`).
  The native limitation is recorded in TODO.

The four recordings share loading, settings, encounter navigation and battle
helpers. Their combined shadow measurements recalibrate only drifted timing
scalars; the field continuations retain documented data-dependent timing
tails. No new game routines were ported; the existing frame-coordinate
store was corrected as described above.
The final narrowed suite selecting these four recordings with `-j 1`
ends `suite OK`: all four shadow replays are clean, both fast routes pass,
all four native-shape replays reach their limits, and the effects check has
zero new findings and zero runtime scratch-state reads. Sound lockstep
matches over 530642 ticks. The strict baselines retain the limitations
above; the successful gate does not claim native boss victories.

Death Peak was looked into before (Targets, 11) and left: the party has to reach the tree's
shelter in the 108 calm frames. `ct_drive`'s machine copies make that a search over the timing.
Crono's return is the next of the story's largest scripted scenes to record.
- **Death Peak movies**: both ctsave11 slot 3 and ctsave12 slot 1 have the Chrono Trigger
  (`$D9`), clone (`$E2`) and Keeper's instructions (`$7F:007C` bits 0-1). The former starts
  outside the mountain and supplies the wind movie above; the latter starts at `$0F6` with the
  guide already a tree and all three Spawn alive (`$7F:0064` bits 3-5 clear).
  `events-death-peak-ascent` (route `death_peak`, 23852 frames) beats two Spawn, opens the
  middle cave from its eastern ledge, crosses the slippery ridge and saves at `$1EF` through
  the cartridge save menu. That save, `oracle/saves/routes/death-peak-summit.srm`, starts
  `events-death-peak` (route `death_peak_return`, 23856 frames): beat the last Spawn, push its
  shell to tile (16, 15), climb it, use the Time Egg at the summit (`$109`), exchange the clone
  for Crono in the frozen Ocean Palace (`$1A7`), and return to the End of Time (`$1D0`) with
  field control. The return flag (`$7F:0057` bit 6) is set and both quest items are consumed.
  Each Spawn is beaten on the first fight attempt by targeting its head with Ice, Fire and
  Uzzi Punch, healing or replenishing MP when needed. Walking and shell positioning use
  machine copies; no memory writes or unsuccessful search inputs enter the movies.
  Coverage adds 2153 ROM instruction bytes over the preceding 48-movie reference coverage.
  Wind/ascent/return add 154/1071/1699 bytes individually; counting the other two new movies
  as well leaves 0/445/1014 unique bytes. The wind covers the first guide's transformation
  and both shelters; ascent adds single- and combination-tech learning in the Spawn rewards;
  return adds circular BG3 effects, shell contact/movement, removal of the quest items and
  large field-object drawing paths. The return's dialogue exposes the `$F9:B000` string table,
  already in the extraction hints but newly included in the asset catalogue.
  `ct_native` has no stop before any movie's frame limit (exit 0; 2/4/4 video escapes,
  first `$7E:5AEB`). Its timing differs, so reaching the frame limit does not prove the route
  outcome. Strict mode also has no stop: wind ends as the reference does, while ascent and
  return drift in their saved game and SRAM, with baselines recorded. No guard-refusal gap
  or fallback-baseline increase is needed.
  Shadow clean for all three: zero mismatches/refusals, no refused page or out-of-range
  accessor; the return's generated-only trace matches all 23856 interpreter frames.
  Drift-only calibration and the remaining field-NMI vblank-tail exception are recorded;
  the video-write check finds no newly visible writes. The tile-decoder's interrupted-call
  baseline records its matching calls in this subset and in the preceding library.
  The narrowed suite over these three movies (`-j 1`), including its static checks, ends
  `suite OK`.

- **Movie**: `events-zeal-palace` (route `zeal_palace`, ctsave08 slot 2,
  20011 frames). Slot 2 has story `$9F`, map `$1F4`, and Schala's meeting flag
  (`$7F:00F4` bit 4) clear; slot 3 is already at `$AB`, after exile. The route
  takes the Skyways, land bridge and mountain teleporters to the palace,
  meets Schala and Janus, watches Schala open the sealed door (`$A2`), and
  charges the pendant at the Mammon Machine. It ends at `$14B`, story `$A5`,
  with control restored. The mountain beams need UP released before arrival
  at their paired beam; the palace's large hall uses directed doorway walks.
  Beyond the preceding 49 movies it adds 190 instruction addresses (423 bytes),
  chiefly the Skyway capsule iris setup and drawing (`$C6:E448`-`$E567`), the
  overworld followers (`$C2:3D0A`-`$3D28`), field movement and green palette
  brightening (`$FD:E98F`-`$E9C9`). The 77 capsule-iris guard refusals at
  `$C6:E498`/`$C6:E4BE` are gaps in the refusal allowlist and TODO.md: their
  centre reaches y=0, outside the guard's supported range. `ct_native` stops
  at frame 4106 on `$C6:E4BE`; strict mode stops there at frame 4109. Shadow
  has zero mismatches, refused pages, out-of-range accessors or uncatalogued
  reads; sound lockstep is identical. Dialogue reads add the `$F9:B000` pointer table
  and its 443 strings to the asset catalogue, preserving the earlier rows.

- **Movie**: `battle-giga-gaia` (route `giga_gaia`, ctsave09 slot 3, 33261 frames):
  the save's story counter is `$B4`, on Mt. Woe's `$189`, with the summit's arrival
  flag (`$7F:01A0` bit 3) clear. Across `$18B` to `$18D`, confirm at the crystal,
  defeat Giga Gaia and its regenerating arms, free Melchior and watch Mt. Woe fall;
  the Terra Cave conversation ends at `$17E`, story `$BA`, with control restored.
  The fight needs no backtrack. The walking search keeps the map level as well as
  position, so it can cross the bridges and the paths beneath them; a held UP at
  the northern chain's end enters the summit. Against the previous 48 movies it
  reaches 137 new instruction addresses (301 ROM bytes): chiefly
  `menu_object_drift` (`$C6:E2B5`-`$E317`), the overworld object sound command
  (`$C2:191F`-`$1946`), the field's walk, scroll and follower poses, and the enemy
  revival's `$C1:9EEE`-`$9EFA`. Shadow clean: zero mismatches, refusals, refused
  pages or out-of-range accessors; sound lockstep identical. Strict mode reaches
  all 33261 frames with saved-game drift (572 bytes). `ct_native` reports no
  stop, but ends blanked after three screen setups; TODO.md records the stalled
  progress, since this does not establish a completed native playthrough.

- **Movie**: `battle-mud-imp` (route `mud_imp`, ctsave09 slot 2, 37817 frames;
  victory at 36527, the end kept at the field back in hand): the settlement
  (`$17C`) into `$184`, the two Beast pairs on rows 47 and 29, and the Mud Imp
  with Blue and Red Beast on row 17. The save's story counter (`$7F:0000`) is
  `$AE`; the boss script requires less than `$B4`, which slot 3 already has.
  The boss policy focuses basic magic on the Imp, using Mid Tonics and Lapis
  to survive; its defeat makes the Beasts leave. All three fights win without
  a wipe. It reaches 570 bytes (0.56 KiB) no movie in the preceding 48 runs,
  all ported: `script_spiral_out` (`$C1:675B`-`$6915`), enemy spacing
  (`battle_enemy_keeps_distance`, `$C1:3AC4`-`$3B5F`), and tech-point counting,
  learning and reward messages (`$C1:F3AF`-`$F532`, `$FD:ADC9`-`$AE04`).
  Shadow clean: zero mismatches and no refusal, `page refused` or `accessor
  out of range`; sound lockstep identical. Strict mode runs all frames with
  no stop, but its final saved game differs (`drifts`). `ct_native` reports no
  explicit stop in 37817 frames; it ends blanked with two video escapes, first
  `$7E:5AEB`, so this is not evidence of a native boss victory.

- **Movie**: `battle-heckran` (route `heckran`, pre-heckran slot 3, 19376
  frames; victory at 17266, the end kept at field control): the cave save
  point (`$031`) into Heckran's chamber (`$02F`), basic magic and healing,
  then the aftermath. The hand-run `heckran_prepare` makes the cartridge
  save normally from ctsave03 slot 3 in 42723 frames; its long movie stays
  out of the library. The source has story `$4D`, the cave save `$4E`, and
  ctsave04 slot 1 `$54`; the boss requires less than `$51`, which the
  aftermath sets. The policy waits while `$7E:B320` is nonzero, including
  the runner's fallback attacks: two water stances waited out (7773-8539
  and 12979-13687), no wipe. Reaches 74 bytes beyond the preceding 48 movies,
  all ported; 62 are unique after Mud Imp, including `battle_enemy_flags_turn`
  (`$C1:9E58`-`$9E5B`) and field sprite drawing. Mud Imp retains 558 unique
  bytes after Heckran. Shadow clean: zero mismatches, no refusal, `page
  refused` or `accessor out of range`. Strict runs all frames without a stop
  but differs in the saved game (`drifts`). `ct_native` reports no explicit
  stop in 19376 frames, with a screen showing and four video escapes, first
  `$7E:5AEB`; a native boss victory is not established by that run.

- **Movie**: `events-cronos-trial` (route `trial`, routes/cronos-trial slot 1,
  21214 frames). Marle is brought home to Guardia Castle (`$015`), Crono is
  arrested, and the courtroom (`$01B`) plays the testimony, the fair
  flashbacks and the jury's seven votes. Confirm selects the first answer to
  each question. Five vote not guilty and two guilty (`$7F:0051 = 2`): Crono
  is acquitted, but sentenced to three days' solitary confinement for running
  away with Marle. The prison transfer crosses `$01C` and `$01D`; the movie
  ends in the cell (`$047`), after Crono wakes and answers the pad, with story
  `$2D` and the trial's phase `$7F:0059 = $10`. The courtroom begins at frame
  4704, the cell loads at 20584 and control is stable at 21214.
  `--coverage` adds 9 live code bytes over the preceding 48 movies, all ported
  (`event_cmd_wait_frames`, `$C0:2FA6`-`$2FAE`); the new coverage is chiefly
  the courtroom's event scripts and jury logic. Shadow: zero mismatches,
  no `page refused` or `accessor out of range`, C sound-driver lockstep
  identical. `field_walk_the_leader` refuses 150 calls during the prison
  transfer, a documented gap in `tools/refusal_allowlist.txt` and TODO.
  `ct_native` stops at frame 20629, `$C0:881E`, "a walk the port doesn't
  take". Strict mode reaches the frame limit but drifts from the reference.
- **Hand run**: `events-cronos-trial-long` (route `cronos_trial`, ctsave01
  slot 2, 136868 frames, `oracle/movies/hand/`, outside the default library).
  No original save sat near the courtroom: slot 2 is on the 600 A.D. world
  map before the Cathedral, and slot 3 is already in prison. The route reuses
  `early-bosses`' Cathedral and Yakra stages, then returns Leene to the castle
  (story `$1C`), restores Marle (`$21`), says farewell to Frog, crosses the
  forest and Truce Canyon, takes the gate to the fair (`$27`), and brings
  Marle home. The normal overworld Save menu writes slot 1 outside Guardia
  Castle at frame 118233; the exported cartridge SRAM is committed as
  `oracle/saves/routes/cronos-trial.srm` (CRC32 `5B31C425`). Loading it from
  power-on produced the short movie above. The hand run's courtroom starts
  at 120358 and its cell loads at 136238. It remains in `HAND_RUN_ROUTES`;
  both movies contain only pad input, with machine-copy retries and no
  diagnostic writes.
  `--coverage` adds 1480 live code bytes over the preceding 48 movies,
  444 beyond those plus `battle-yakra`, all ported. The latter includes
  counted moves towards objects (137 bytes) and party members (104),
  `overworld_region_song` (93), and enemy approach, distance and turn paths.
  Shadow: zero mismatches, no `page refused` or `accessor out of range`,
  C sound-driver lockstep identical. There are 913 guard refusals at six
  entries: the 600 A.D. forest's movement and battle setup, the castle's
  overworld exit and the transfer to prison. Each is a gap documented in
  TODO and the refusal allowlist; none was ported for this recording.
  `ct_native` reaches 136868 frames without an explicit stop, with the
  screen blank; it does not establish a native completion. Strict mode
  reaches the frame limit but drifts from the reference saved game and SRAM.

### The unequipped-armour glitch (Target 14)
- **Movie**: `battle-unequipped-armor` (route `unequipped_armor`, ctsave04 slot 2, 22859
  frames; last damaging hit 21969, then Spekkio's dialogue and the field back in hand). This
  save is outside Denadoro with no spare armor, helmets or weapons, but spare accessories.
  First visit an empty inventory row: this leaves the item operand at zero through normal
  controls, avoiding the existing port's scratch-byte mismatch on an empty first equipment
  list (original preserves `$7E:04C9`, native clears it; this save initially leaves `$F1`).
  On Crono's accessory slot, R+Up changes to Marle's armor; A twice equips the empty list's
  item zero. L returns to Crono's armor, which now lists the armor Marle removed; R+Up and
  A twice empty Marle's helmet. The party crosses Zenan Bridge, wins both Truce Canyon
  ambushes, takes the gate to the End of Time, and beats the 800-HP Spekkio with Marle's Ice.
  No memory writes or edited saves; the searches restore machine copies and keep only the
  chosen inputs. The comparison restores the pre-fight room, puts her Iron Helm and Titan
  Vest back on through the menus, and wins the same fight; it is not another library movie.
- **The menu bug**: `game/menu/equipment.c`, `menu_equip_list_is_for_another_slot`
  (`$C2:9ECD`'s sequence), caches only `menu_equip_slot + menu_member_worked_on`.
  R+Up or L+Down without wrapping changes the two in opposite directions, keeping that
  sum and the old `menu_equip_choices`. `menu_equip_choose_slot` (`$C2:9C70`) accepts the
  stale nonzero count. Opening the list rebuilds it (`$C2:8953`), now containing only its
  zero terminator, and `menu_equip_put_on_the_chosen_item` (`$C2:9CC1`) returns the worn
  piece to the inventory and stores zero in the character's slot. A replacement that fits
  prevents the empty selection, which is why this save needs no selling first.
- **The battle bug**: `game/battle/member_stats.c`, `set_up_member_stats` (`$C1:CE3A`),
  subtracts the category's first item in a byte even when the item is zero. Armor uses
  `$08C0 + 6 * uint8_t(0-$5A)` and helmets `$0986 + 6 * uint8_t(0-$7B)`; both select
  `$CC:0CA4`. Its boost index `$21` selects +10 magic (`$FD:B141`, `$FD:B14D`), and its
  affinity `$EF` adds 15 to the first three elements (`$C1:CF69`), excluding Fire.
  `effect_modifiers.c`'s element modifier (`$C1:EF39`) divides by that affinity; an attack
  without an element skips it. These are hidden resistances, not the magic-defence stat.
  Marle's magic is 41 empty versus 21 equipped, and affinities are `[34,34,35,4]` versus
  `[4,4,5,4]` (Lightning, Shadow, Water, Fire). `effect_amounts.c`'s magic formula
  (`$C1:E0F5`) uses `power * 12 * (level + magic) / 15`, followed by variance, defence
  and modifiers: a fixed doubling of outgoing damage is not the rule.
- **Measured**: empty, Marle's four Ice numbers are 244, 245, 246, 246 (the last removes
  Spekkio's remaining 65 HP); equipped, her six are 158, 157, 158, 157, 157, 159.
  Spekkio's Ice (`$2A`) takes 15 from her empty versus 108 equipped; Lightning (`$2B`)
  takes 20 versus 171. His weaker Water attack (`$0D`) also takes 11 in the movie.
  The ratio expected before variance is 5/35 for Water and 4/34 for Lightning: about
  86% and 88% less damage. The equipped comparison's Fire (`$25`) takes 134; an empty
  diagnostic attempt took 130, consistent with Fire's unchanged divisor of 4. Damage
  is read from the displayed-number records and checked against HP changes; the two
  fights have different random variance. The description and button combination are
  also documented by [Chrono Speedruns](https://chrono.wikidot.com/glitches) and
  [inichi's original account](https://tasvideos.org/Forum/Topics/7364?CurrentPage=1&Highlight=181766).
- **Coverage and validation**: 121.04 KiB of ROM code in `--coverage`, including
  502 bytes (0.49 KiB) absent from the other 48 movies' coverage; no new RAM-code
  entry addresses. These include the higher-affinity branches (`$C1:CF7F`,
  `$C1:CFA1`), farthest-member targeting (`$C1:A4E0`), family-dependent status
  effects (`$C1:EED0`), enemy/field movement, and the inventory's downward row
  slide (`$C2:AC6E`-`$C2:AD14`, 156 bytes). Shadow: 0 mismatches, no `page refused`,
  invalid accessor or asset-range error; C sound lockstep identical (70395 ticks).
  The empty equipment-list drawing guards refuse 44 calls at four entries;
  shipping and strict playback encounter `field_press_confirm`'s guard at frame
  7594, `$C0:1AAC`. These are gap lines in `tools/refusal_allowlist.txt` and
  `docs/TODO.md`, with the five new shadow fallback routines added to the existing
  baseline. Strict stops there; `ct_native` stops earlier, at frame 3147,
  `$C2:AC3A`, the inventory-list slide's machine-only continuation. The route was
  run from the save through its end with the interpreter, producing this movie.
  Timing was recalibrated with `--calibrate game/registry.c --drifted` on this
  narrowed run; the remaining dispatcher and timing-tail limits have measured
  reasons in `tools/timing_exceptions.txt` for the coordinator's combined gate.
  The scroll-bar task's abandoned comparisons are recorded in the uncompared
  baseline. The 7200-frame generated-only/reference trace check was run serially
  (no divergence); `suite.py --no-trace` avoids its two concurrent trace processes.
  The narrowed suite (`-j 1 --movies battle-unequipped-armor --no-trace`) ends
  **`suite OK`**, including its static checks, shipping vblank check, sound fuzz,
  and movie/fast-route checks with scaffolding compiled out. No full suite was run.

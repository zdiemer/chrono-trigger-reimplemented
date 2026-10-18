# Movies wanted

The game code no movie in the library executes, grouped by what sets it off in
the game, and the movies that would reach it. Shadow mode only checks what a
movie runs, so code nobody runs rests on fuzzing alone (TODO.md, "Movies and
coverage"). This is a ledger for picking the next movie to record; grep it by
cluster, save or address.

## How it was measured

Against the commit that added this file, over the 27 movies of the library
(`menus-shop-equip` included); the world maps' figures moved to ported where
the overworld on foot landed (docs/history/reimplementation-chunks.md, "Phase 3, overworld"):

- **Code**: the analysis `tools/burndown.py` runs (its `Analyzer`, with
  `reached_code`), so code only an `unreached` hint starts is left out.
  **Executed**: the union of the movies' `--native shadow --coverage` files.
  **Ported**: the bytes the registry's ports cover (`burndown.coverage`).
- Each unexecuted instruction is owned by the port whose walk reaches it (its
  own region first, then what it inlines), else by the smallest routine of the
  analysis holding it. The owner's name and sources say what it is.
- The trigger of each owner, by kind:
  - world-map objects (bank `$C2` below `$8000`, `$C6:E000` on): the scene each
    object script belongs to (`tools/disasm/objscript.py`; scene *n* is world
    map location `$1F0`+*n*, whose era the Epoch's map view names);
  - the scene program (`$7E`): the modes of `$7E:300A`, by what each mode's
    setup reaches directly and through the sequence scripts it starts
    (`tools/disasm/seqscript.py`); the event scripts that start a mode (event
    `$FF` below `$90`, the mode in its low bits) give its locations;
  - enemy AI: the enemies whose scripts name the condition, action or target
    (`tools/disasm/battlescript.py`, names from `$CC:6500`);
  - event commands: the locations whose event scripts use the opcode
    (`tools/disasm/derive.py`'s `event_scripts`);
  - the attract demo: `ct_drive` idling at the title, `$7E:0384` (the scene
    mode) and `$7E:0100` (the location) read every 50 frames.
- A save's location is the word at `$5F3` of its slot in the `.srm`.

"Reaches" below is what a target's play passes through, so it is an upper
bound: a cluster's branches may depend on details the play doesn't hit.
Re-measure after recording with the movie's `--coverage` file. KiB per minute
is that bound over the play's length, for ordering only.

## Totals

| | KiB unexecuted | ported | unported |
|---|--:|--:|--:|
| all live code not run by any movie | 57.8 | 45.2 | 12.6 |

## Clusters

`cluster | KiB | ported KiB | trigger`. Locations are `$LLL` (era from
`$FF:D296`: 1 1000 A.D., 2 600 A.D., 3 2300 A.D., 4 65,000,000 B.C., 5 12,000
B.C., 6 the Black Omen, 7 the End of Time, 0 1999 A.D. and the title's).

| Cluster | KiB | Ported | Trigger |
|---|--:|--:|---|
| battle animation commands | 8.70 | 8.70 | rare commands of tech, attack and item animation scripts (`script_*`, `battle_screen_script_*`, `$C1:2D9F`'s inlined handlers); which techs use them isn't mapped (the five header tables at `$CD:5EC6`-`$CD:63F0` aren't tied to tech ids) |
| field engine | 5.43 | 5.25 | paths no map in the library needs: large (8-, 12-, 24-sprite) objects' frames put back after a battle or menu (`field_put_drawn_frame` `$C0:CB0A`), `field_walk_the_leader`, `field_redraw_after_picture`, `field_stream_the_layers`, `field_update_object_sprites`; maps unknown |
| scene modes | 7.93 | 1.18 | see the scene program rows below |
| world-map objects | 10.01 | 6.66 | see the world map rows below |
| enemy AI, by enemy | 1.95 | 1.95 | see Bosses below |
| enemy AI no script names | 2.79 | 2.79 | conditions and target selectors no enemy script uses (`battle_condition_acted_on_by_member`, `stands_beside`, `level_with`, `stat_bits`, `could_walk_to`, `is_party_member`, `frames_at_least`, `turn_wait`, `three_on_out`; most `battle_select_*` by status); reachable only through techs' targeting, if at all -- unknown |
| battle other | 3.15 | 3.11 | `battle_enemy_tech_body` branches, battle windows and cursors, status updates, `battle_frame_work`, `battle_nmi`; unknown |
| event commands | 2.89 | 2.87 | see Event commands below |
| menus: other | 2.34 | 1.28 | the party exchange's panel (`$C2:C85B`-`$CCB3`, generated), cursors on every member (an item used on the whole party), `menu_party_services` `$C2:8D8F` |
| battle action effects | 2.17 | 2.17 | `battle_effect_hit` (1.5 KiB of branches), `inflict`, `steal`, `restore_showing_hp`: uncommon tech and item effects |
| floor switches | 2.10 | 2.10 | stepping on a floor switch of a shape no movie presses (`$C0:0D45`-`$C0:16DB`); only `endings-telepod-lavos` presses one; which maps hold the others is unknown |
| enemy movement | 1.28 | 1.28 | enemies that line up, march, charge, keep their distance (`$C1:375C` family) and the walk searches (`$C1:2C02`, `$2CF3`); which enemies is unknown (movement types aren't in the AI scripts) |
| field screen effects | 1.22 | 0.63 | event `$FF` screen forms: the ripple (`$C0:264F`), closing into a scene, `$C0:405D`-`$4145`, `$C0:28F9`; locations not mapped |
| menus: items | 0.77 | 0.77 | item screen tabs and members (`menu_use_tab`, `menu_look_at_tab_member`, `menu_raise_figure`, `menu_show_item_users`) |
| sound driver | 0.59 | 0.59 | `sound_driver_command` branches (`$C7:02E2`, `$C7:0181`): commands no movie's scene sends |
| battle rewards | 0.55 | 0.45 | learning a single or combination tech, tech points counted to a new tech, `$FD:AD17` |
| menus: tech screen | 0.52 | 0.52 | scrolling the tech screen past three members or eight techs (`menu_slide_tech_*`) |
| menus: settings buttons | 0.49 | 0.23 | Settings, custom button assignment (`menu_choose_buttons` `$C2:BF6D`, `$C2:C20F`-`$C286`) |
| palette effects | 0.46 | 0.46 | `palette_lighten_*`/`darken_*`, `palette_pulse_*`: event palette commands; scripts not mapped |
| text control codes | 0.45 | 0.45 | typesetter codes no movie's text uses (`$C2:5953`-`$5C77`) |
| new game plus | 0.36 | 0.36 | starting a New Game+ (`menu_load_carried_over` `$FF:FAED`, `$C2:E6AE`, `$C2:E841`) |
| menus: equipment | 0.30 | 0.30 | the equipment screen's member slide (`$C2:9DAF`) and switching member there |
| battle status timers | 0.34 | 0.34 | MP regained (`$C1:8B10`), Slow (`$C1:8975`), HP lost, evasion timers running out: statuses no movie's fights inflict |
| load screen erase | 0.26 | 0.26 | erasing a slot, and answering no (`$C2:E8B7`, `$C2:E705`) |
| demo battle | 0.20 | 0.20 | the attract demo's battle (`fight_load_demo_party` `$FD:B2DE`, `battle_demo_next_turn`) |
| epoch screens | 0.20 | 0.20 | time dial and map view branches (`$C2:6AC8`, `$C2:6736`) |

### The scene program (`$7E`, modes of `$7E:300A`)

| Mode | KiB (unported) | Started by |
|---|--:|---|
| 5 (`$7E:7092`) | 2.05 | no event script; not seen in the attract demo; unknown |
| 6 (`$7E:78DD`) | 1.40 | event `$FF $26` at `$1B0` (era 0), a location the attract demo passes without starting it; unknown otherwise |
| 10/11/14/15 (`$7E:8538`) | 0.74 | the endings (`$FF $2A`/`$2B` at `$051`, `$2E`/`$2F` at `$05A`); paths the three recorded endings don't take |
| 3 (`$7E:671F`) | 0.62 (+0.16 shared with 5) | the attract demo (`$0384` = `$63` after location `$06B`); event `$FF $23`/`$63` at `$066`-`$06B` (era 6) |
| 7 (`$7E:6248`) | 0.50 | event `$FF $07` at `$1DC` (era 0, beside the final battle's `$1DA`) |
| 4 (`$7E:53BB`) | 0.46 | event `$FF $24` at `$12D` (era 4, beside ctsave08 slot 1's `$12F`, Azala and the Black Tyrano) |
| 1 (`$7E:6B04`) | 0.39 | the attract demo (`$0384` = `$61`); event `$FF $61` at `$1AF`/`$1B1` (the title's), `$0AD`, `$16E` |
| unattributed | 0.89 | race tasks and program routines no mode's setup reaches statically |

### World maps (object scripts of the `$C2:0000` mode)

| Map | KiB (ported) | Saves on it |
|---|--:|---|
| 65,000,000 B.C. (`$1F3`, scene 3) | 2.63 (0.38) | ctsave05 slots 1, 2; ctsave07 slots 2, 3; ct-state6, ct-state10; zophar chrono-4 slots 2, 3. Handlers `$C2:4CC5`, `$5097`, `$5494`, `$504B` and `$C6:E072`/`$E101` |
| 1000 A.D. (`$1F0`, scene 0) | 1.67 (1.54) | every 1000 A.D. save; the code is `$C6:E7FA`-`$EE30`, routines a scene 0 script calls with command `$4E`: the Day of Lavos after a loss to Lavos, which `battle-lavos-gameover` (route `lavos_lost`) now plays; commands `$3F`/`$40`/`$08`/`$25` no movie's walk there runs |
| 12,000 B.C. (`$1F6`, scene 6) | 0.78 (0.78) | zophar chrono-6 slot 2 |
| 12,000 B.C. (`$1F4`, scene 4) | 0.54 (0.46) + 0.21 shared with 1, 5 | ctsave08 slot 2; ct-state7, ct-state8 |
| 1999 A.D. (`$1F7`, scene 7) | 0.23 (0.23) | none; the bucket ending and the attract demo pass over it |
| 12,000 B.C. (`$1F5`, scene 5) | 0.19 (0.16) | zophar chrono-5 (all three slots) |
| shared by several maps | 1.82 (1.35) | the overworld on foot (`game/overworld/`, ported): an exit to an event (`$C2:2417`), the party's exchange opened from the map, a walk into another region's song (`$C2:3011`-`$C2:309D`), the halts, the walkers' steps while an event's scripts run; generated: the vehicles' controller (`$C2:309E`) and routines several maps' scripts call |
| scene not attributed | 1.94 (1.44) | object commands no scene's script has (`show_tile_group` `$50`, `copy_byte_slot_to_slot`, `branch_if_slot_no_bit`) and code beside them |

### Bosses (enemy AI by script)

| Enemy | KiB | What only it (or few) uses |
|---|--:|---|
| MotherBrain | 0.49 | action `$15` add-to-stats-then-tech (`$C1:A20B`), selector members with element `$08` |
| Guardian (with Lavos, Giga Gaia) | 0.37 | action `$10` bring the beaten back (`$C1:9E78`), farthest enemy, slot 8 |
| Nizbel II | 0.36 | add-to-stats (`$C1:A188`), stat at least, members at most |
| Mad Bat, Bug, Flea | 0.20 | condition with-status (`$C1:8F11`), other kind present |
| Blue Beast, Red Beast | 0.18 | at half HP, acted bit, die (`$C1:9CB3`, also Mud Imp, Retinite, Turret) |
| Alkaline, Shadow | 0.16 | within reach, is-enemy, nearest other enemy |
| R Series | 0.07 | stands-in (`$C1:93E6`) |
| enemy `$EF` (unnamed), Lavos, Magus | 0.06 | call-in (`$C1:A396`), slot 2 |

### Event commands (by opcode's locations)

| Command | KiB | Locations whose scripts use it |
|---|--:|---|
| `$98` move toward object, counted | 0.19 | `$008`, `$192`, `$100` |
| `$9A` move to tile, counted | 0.15 | `$1A3`-`$1A7`, `$195` |
| `$9E` move toward object, keep facing | 0.13 | `$0CC`, `$1A3`-`$1A7`, `$029` |
| `$D6` take out of the party | 0.09 | `$0E6` |
| `$9D` move by variables, keep facing | 0.08 | `$005` |
| `$28` if well on screen | 0.08 | `$16B` |
| `$14`/`$15`/`$13` compare variables, words | 0.19 | `$005`, `$0DF`/`$0E1` (the race), `$1B2`, `$08F`, `$195`-`$1A1` |
| `$5E`, `$61`, `$60`, `$72`, `$76`, `$50`, `$52`, `$54`, `$59`, `$4D` word arithmetic | 0.40 | `$005`, `$1B2`, `$08F`, `$0DF`/`$0E1`, `$172`-`$17B`, `$199` |
| `$63`/`$64`/`$69`/`$6B` flags by number | 0.16 | `$1B2`, `$0DF`/`$0E1`, `$066`-`$06B`, `$086`/`$087`, `$1D1` |
| `$C4` ask (bottom window) | 0.05 | `$0F2`, `$0F3`, `$13B`, `$07A`, `$1D1` |
| `$D7`, `$C7`, `$CD`, `$CB` items and gold | 0.12 | `$115`; `$175`-`$1BC`; `$1B7`, `$032`, `$09A` |
| `$DC` location put aside | 0.04 | `$0E4`, `$0E5` |
| `$7B` launch, `$5D` add variable | 0.13 | no event script uses them |

## Targets

Ranked by KiB reachable per minute of play. **Route**: whether a route script
(`tools/movies/routes/`, MOVIES.md) could play it instead of a person.

### 1. The attract demo
- **Recorded**: `attract-demo.ctm`: power-on, no input, 22500 frames. A cycle
  is 11180 frames (the title again at 11290 and 22470); the second pass runs a
  little the first doesn't, and running on to 198,000 frames adds five
  instructions of frame and sound timing.
- **Reached**: 3.89 KiB no other movie runs, 2.35 of it ported (against 4.98
  predicted): the scene program's modes 3 and 1 and code they share (1.25 KiB,
  unported, most of it `$7E:66DD`-`$7E:6CAB`) and the race without its panel (its curtain, `scene_race_set_up`'s
  other branch); the field's curtain, the screen effect's step (`$C0:22D8`),
  bank `$C3`'s sweep and event `$FF` `$92`-`$95` (`$C0:405D`-`$C0:4145`); the
  demo battle (`fight_load_demo_party`, `battle_demo_party_ready`,
  `battle_demo_next_turn`, now calibrated) and its screen scripts' swing and
  bob (`battle_screen_script_swing`, `battle_screen_swing`,
  `battle_special_bob_layer_up`); and field and world-map paths beside them
  (`field_read_animation_frame`, `field_stream_the_layers`,
  `menu_object_read_scene_mode`). Mode 5's and mode 6's own code and the race's
  finish stay unreached.
- **Shadow**: found the curtain's call made a frame above the original's, 31
  mismatches (`field_run_table_step`, PORTING.md); every call matches now.

### 2. Every tech
- **Save**: zophar chrono-5 (all but Magus maxed) and zophar chrono-trigger (all techs).
- **Do**: in a long fight against something weak, cast every single, double and
  triple tech once, Magus's too.
- **Reaches**: an unknown part of the animation commands (8.70 KiB), action
  effects (2.17) and screen scripts; up to 10.87 KiB, about 0.7 KiB a minute.
  The rewards' tech learning (0.55) wants a save with tech points to earn instead.
- **Route**: yes (the battle bot with a policy that walks the tech lists).
- **Route written, not merged**: `tools/movies/routes/tech_sweep.py` (zophar chrono-trigger slot 1:
  Crono, Magus and Marle inside Lavos's shell): walks to the second Lavos form, fights it and then the
  Lavos Core with a policy (`tools/movies/policies.tech_sweep`) that opens Tech on each member's turn and
  takes the first list entry -- by tech id, read from the tech-by-id table at $94D0, not list position,
  so a double tech is found once whichever partner's turn reaches it first -- that isn't cast yet,
  falling back to Attack once a member's list is exhausted for now (not enough MP, or a tech whose only
  target needs an ally down); heals and revives with items. Casts the three members' 12 single techs and
  8 double techs between them (ids 1-5, 9-15, 49-56), each once, well inside the first third of the
  fight; no triple tech appears for this trio. Its recording, cut to the shortest length (found by
  bisecting the `--coverage` cutoff) that still reaches everything it reaches -- the Lavos Core down to
  its last few hits, not beaten outright, since that's as early as two of its addresses (inside
  `battle_select_random_member`, an enemy target selector) are reached, an artifact of the Core's own AI
  nearing death rather than of the sweep's tech order -- is 140500 frames and would reach 0.84 KiB (406
  addresses) no other movie runs, in 28 routines, mostly in `$C1`: the three largest are
  `battle_work_out_enemy_attack`, `script_point_along` and `battle_apply_damage` (uncommon branches
  inside routines every battle already reaches), plus a few reached for the first time outright,
  `battle_effect_anchor_chase` and `fight_apply_accessory_effects` among them. Shadow clean, but
  **not added to `oracle/movies/saves/`**: it also makes `script_return_from_call`'s guard
  (`command_ready`, turning away a battle script state it doesn't model) refuse 8 times, a real gap
  (docs/TODO.md, "Refusals in real play") rather than an allowlist entry, and the gate's refusals check
  fails on it. Record the movie once that guard's gap is closed.
- **Not attempted**: zophar chrono-5 (Crono, Marle and Ayla, all but Magus maxed). Its only slot (all
  three point to the same save) stands on an isolated 12,000 B.C. floating island: `ow_bfs` finds its
  whole reachable ground is 57 tiles with no door out and, over 20 laps of it, no random encounter --
  this era's overworld looks to have none. A second movie for Lucca, Robo, Frog and Ayla's own techs,
  the techs between them, and any triple tech (this trio and Crono/Magus/Marle have none) needs a
  different starting point than the save gives.

### 3. Menu sweep
- **Save**: ctsave14 slot 1 (seven members).
- **Do**: tech screen scrolled past three members and eight techs; equipment
  changed member to member from its slide; items: the tabs, the users list, an
  item used on the whole party; Settings, a custom button assignment; save from
  a save point (the save screen's own opening, `$C2:D4F8`).
- **Reaches**: up to 2.08 KiB (1.82 ported) of the menu clusters, about 0.4 KiB a minute.
- **Route**: possible (the cursors are in RAM); by hand is quicker.

### 4. New Game+ and an erased slot
- **Save**: technoplaza ct-state16 (a finished cartridge).
- **Do**: on the load screen, erase a slot (answer no, then yes), then start a New Game+.
- **Reaches**: 0.62 KiB (ported), about 0.3 KiB a minute.
- **Route**: yes (load screen presses read from RAM).
- **Movie**: `menus-new-game-plus` (route `new_game_plus`, 3116 frames): on the second slot, Select
  and Start on the second controller (`P2:` buttons, through `ct_drive`'s second pad) ask to erase it,
  no keeps it, a second ask and yes erases it; then the fourth row's New Game+, carrying on from the
  first slot, the default battle mode and Crono's own name, onto the 1000 A.D. world map. It reaches
  0.67 KiB of code no other movie runs (`$C2` and `$FF`). Shadow mode found four port bugs on the
  way, all fixed: the party's services handed back 16-bit index registers to a New Game+'s byte-wide
  caller; flipping the fourth row's choice and a no to the erase question left the cursor sound's
  saved flags and A's high byte unlike the original's (fuzzing the same paths found the fourth row's
  new game doing the same); and the New Game+ question's guard refused the byte-wide call the load
  screen makes.

### 5. Boss fights
- **Saves**: Guardian at Arris Dome, ctsave02 slot 3 (`$0D6`); R Series in the
  factory, ctsave03 slot 2; Nizbel II in Tyrano Lair, ctsave07 slot 3;
  MotherBrain in Geno Dome, ctsave14 slot 2 (slot 3 is after it).
- **Do**: walk to the boss and win.
- **Reaches**: 1.29 KiB of enemy AI (ported) for the four, plus whatever of the
  animation commands and effects they use; about 0.1 KiB a minute on the AI alone.
- **Route**: yes (walking, `talk()`, the battle bot).

### 5b. The Day of Lavos after a loss
- **Save**: technoplaza ct-state16 slot 2 (Crono and Marle at the fair, New Game+).
- **Do**: take the right telepod's Gate to Lavos and lose the shell's fight.
- **Recorded**: `battle-lavos-gameover` (route `lavos_lost`, 16192 frames): nobody acts until the
  shell's first imitation wipes the party; the loss plays Lavos rising out of the 1999 A.D. world map,
  the operators' room, the 1000 A.D. world map laid back and turned in Mode 7 under the needles
  (`$C6:E7FA`-`$C6:EE30`, which no other movie's reference play reaches: the shipping mode's endings
  drifted into it after a lost Lavos fight, 7.3 M instructions run as the original), and the globe
  behind "But... the future refused to change", where the movie stops.

### 6. Black Tyrano and Lavos's fall
- **Save**: ctsave08 slot 1 (`$12F`).
- **Do**: beat Azala and the Black Tyrano; watch the scene at `$12D`.
- **Reaches**: mode 4 (0.46 KiB unported) and the Black Tyrano's AI; about 0.08 KiB a minute.
- **Route**: yes (walking and the battle bot).

### 7. The other endings
- **Saves**: technoplaza ct-state17 and ct-state19 to ct-state26 (New Game+ at each ending's story point).
- **Do**: beat Lavos at that point and watch the ending.
- **Reaches**: mode 10's unrun paths (0.74 KiB), mode 7 at `$1DC` (0.50); which
  ending starts modes 5, 6 and 12 (3.47) is unknown. Up to 4.71 KiB over nine
  endings of 30-50 minutes each, about 0.01 KiB a minute.
- **Route**: yes (`bucket_lavos`, `telepod_lavos` and `beyond_time` are the pattern).

### Unknown
- **Floor switches** (2.10 KiB): no map with a switch of the other shapes is identified.
- **Mode 5** (2.05 KiB): nothing found that starts it.
- **Field engine**, **battle other**, **enemy movement**, **sound driver** and
  **palette effects** (10.9 KiB together): no trigger identified; expect the
  targets above to reach some of them incidentally.

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
| re-measured over the 42 movies of chunk 9 (`menus-sweep`, `menus-save-point`, `events-epoch-lavos`, `battle-black-tyrano`, `battle-guardian` and `battle-tech-sweep` added), with that commit's ports | 40.9 | 37.3 | 3.7 |
| the same commit without those six movies | 47.4 | 42.3 | 5.1 |
| re-measured at the same commit for the second batch (each movie's own `--coverage`, the union; Clusters) | 40.92 | 37.25 | 3.67 |
| with the second batch's six movies (`battle-tech-sweep-spekkio`, `field-65m-bc-dactyl-flight`, `field-floor-switch`, `battle-menu-buttons`, `field-factory-switch`, `field-northern-ruins-switch`; in the library since chunk 11) | 36.31 | 32.65 | 3.67 |

## Clusters

`cluster | KiB | ported KiB | trigger`. Locations are `$LLL` (era from
`$FF:D296`: 1 1000 A.D., 2 600 A.D., 3 2300 A.D., 4 65,000,000 B.C., 5 12,000
B.C., 6 the Black Omen, 7 the End of Time, 0 1999 A.D. and the title's).

Re-measured at the start of the second batch of movies (commit 7ad4bbe, the 42 movies, each
`--native shadow --coverage`, all clean): 40.92 KiB unexecuted, 37.25 ported. Owners as above; the
cluster is read from the owner's name and bank -- `script_*`, `battle_screen_*`, `battle_special_*`
and banks `$CD`/`$CF`/`$D1` the animation commands, `battle_effect_*` the action effects,
`battle_condition_*`/`battle_select_*` the AI no script names, `battle_enemy_*_turn` the AI by
enemy, the other `battle_enemy_*` and `battle_search_*` movement, `menu_object_*`, `overworld_*`
and bank `$C2` below `$8000` (but the typesetter) the world maps, bank `$7E` the scene program --
so a few owners moved between the battle rows against the first measure: the area searches and
the aiming (`battle targeting`) were counted with the animation commands before.

| Cluster | KiB | Ported | Trigger |
|---|--:|--:|---|
| battle animation commands | 5.94 | 5.91 | rare commands of tech, attack and item animation scripts (`script_line_to_a_place`, `script_spiral_*`, `script_chase_to_coordinates`, `battle_screen_script_*`); which techs use them isn't mapped (the five header tables at `$CD:5EC6`-`$CD:63F0` aren't tied to tech ids) |
| world-map objects | 5.14 | 5.14 | see the world map rows below |
| field engine | 4.12 | 3.98 | paths no map in the library needs: `field_redraw_after_picture` `$C0:ED31`, large objects' frames put back after a battle or menu (`field_put_drawn_frame` `$C0:CB11`), `field_walk_the_leader`, `field_update_object_sprites`, `field_draw_object_sprites`; maps unknown |
| scene modes | 3.57 | 0.65 | see the scene program rows below (older than this total) |
| enemy AI no script names | 3.29 | 3.29 | conditions and target selectors no enemy script uses (`battle_select_members_by_condition`, `battle_condition_acted_on_by_member`, `stands_beside`, `level_with`, `stat_bits`, `could_walk_to`, `is_party_member`, `is_enemy`); reachable only through techs' targeting, if at all -- unknown |
| event commands | 2.86 | 2.86 | see Event commands below |
| battle other | 2.55 | 2.55 | `battle_take_menu_input`, `battle_draw_window`, `battle_show_pose_look`, `battle_update_combatant`, `battle_update_combo_members` branches; unknown |
| floor switches | 2.19 | 2.19 | stepping on a floor switch of a shape no movie presses (`field_handle_requests`'s `$C0:0D45`-`$C0:16DB`) and the map's pressed switches put back as it loads (`field_press_the_remembered_switches` `$C0:28EC`); see Targets, 10 |
| battle action effects | 1.61 | 1.61 | `battle_effect_hit` (1.2 KiB of branches), `inflict`, `restore_showing_hp`, `steal`, `restore`: uncommon tech and item effects |
| menus: other | 1.57 | 1.11 | the party exchange's reserve scroll (`$C2:C85B`-`$C2:C972`, generated; not reachable, Targets 3), a shop's how-many question (`menu_choose_how_many_to_buy`), `menu_draw_text`, the naming grid's moves |
| battle targeting | 1.57 | 1.57 | areas no cast tech's targeting has searched (`battle_find_the_noted_area` `$C1:2332`: beams, circles, the party's triangle, rows), `battle_aim_action`, `battle_choose_targets`: area techs and items |
| enemy movement | 1.33 | 1.33 | enemies that line up, march, charge, keep their distance (`$C1:375C` family) and the walk searches (`$C1:2C02`, `battle_search_nearer_to_third`); which enemies is unknown (movement types aren't in the AI scripts) |
| enemy AI, by enemy | 1.16 | 1.16 | see Bosses below |
| field screen effects | 0.65 | 0.63 | event `$FF` screen forms: `field_move_screen_effect_on` `$C0:2307` (the ripple `$C0:264F` and the effects beside it), `$C0:28C0`; see Targets, 10 |
| sound driver | 0.59 | 0.57 | `sound_driver_command` branches (`$C7:02E2`, `$C7:0181`): commands no movie's scene sends |
| battle rewards | 0.52 | 0.52 | tech points counted to a new tech, learning a single or combination tech (`rewards_count_tech_points`, `rewards_learn_*`), `battle_show_the_rewards` `$FD:AD41` |
| palette effects | 0.46 | 0.46 | `palette_lighten_*`/`darken_*`: the channel modes of event command `$2E`/`$88` fades (Targets, 10) |
| text control codes | 0.45 | 0.45 | typesetter codes no movie's text uses (`menu_text_go_on`, `$C2:5953`-`$5C77`) |
| menus: items | 0.37 | 0.37 | `menu_choose_on_item_list`, `menu_use_one_more` (a second of an item used at once), setting items aside |
| battle status timers | 0.23 | 0.23 | MP regained (`$C1:8B10`), evasion, HP lost, poison and Stop timers running out: statuses no movie's fights inflict |
| epoch screens | 0.20 | 0.20 | time dial and map view branches (`menu_epoch_time_dial_object` `$C2:6B95`, `menu_epoch_map_matrix`) |
| menus: exchange | 0.16 | 0.16 | the exchange laid out and its cursor moved where no movie's exchange goes (`menu_lay_out_the_exchange` `$C2:CB3F`) |
| menus: equipment, tech screen, settings buttons, load screen erase | 0.21 | 0.21 | the leftovers of the menu sweep's rows: the equipment list's slide, a tech chosen, a button's question closed, the erase |
| other | 0.21 | 0.12 | `$C3:0A8E`, the scene program's boot and frame |

### The scene program (`$7E`, modes of `$7E:300A`)

| Mode | KiB (unported) | Started by |
|---|--:|---|
| 5 (`$7E:7092`) | 2.05 | no event script; not seen in the attract demo; looks dead (Targets, 8) |
| 6 (`$7E:78DD`) | 1.40 | event `$FF $26` at `$1B0` (era 0): the winged Epoch flown into Lavos over 1999 A.D.; `events-epoch-lavos` (Targets, 8) |
| 10/11/14/15 (`$7E:8538`) | 0.74 | the endings (`$FF $2A`/`$2B` at `$051`, `$2E`/`$2F` at `$05A`); paths the three recorded endings don't take |
| 3 (`$7E:671F`) | 0.62 (+0.16 shared with 5) | the attract demo (`$0384` = `$63` after location `$06B`); event `$FF $23`/`$63` at `$066`-`$06B` (era 6) |
| 7 (`$7E:6248`) | 0.50 | event `$FF $07` at `$1DC` (era 0, beside the final battle's `$1DA`): the Day of Lavos after a loss, which `battle-lavos-gameover` (added after this row) runs; 42 bytes left, mode 8's branch among them (Targets, 8) |
| 4 (`$7E:53BB`) | 0.46 (ported in chunk 9 but the pixels' spread `$7E:350E`) | event `$FF $24` at `$12D` (era 4, beside ctsave08 slot 1's `$12F`, Azala and the Black Tyrano); `battle-black-tyrano` (Targets, 6) |
| 1 (`$7E:6B04`) | 0.39 | the attract demo (`$0384` = `$61`); event `$FF $61` at `$1AF`/`$1B1` (the title's), `$0AD`, `$16E` |
| unattributed | 0.89 | race tasks and program routines no mode's setup reaches statically |

### World maps (object scripts of the `$C2:0000` mode)

Re-measured with the clusters: each unexecuted byte goes to the nearest routine at or before it that
some scene's object script calls (commands `$34`, `$4E`, `$35`/`$42`; `tools/disasm/objscript.py`), and
to that routine's scenes; the rest is the object commands' own handlers and the overworld's code.

| Map | KiB (all ported) | Saves on it, and what is left |
|---|--:|---|
| 65,000,000 B.C. (`$1F3`, scene 3) | 0.53 (+0.11 shared with 6) | ctsave05 slots 1, 2; ctsave07 slots 2, 3; ct-state6, ct-state10; zophar chrono-4 slots 2, 3. The dactyls' handlers `$C2:4CC5` (the one ridden) and `$C2:5097` (the two that follow), `$C2:5494` (started on the scene's other branch, `$7F:048B`), and the scene's cutscenes' far calls `$C6:E072` (`$7F:071E`, after the map is redrawn and scrolled) and `$C6:E101` (`$7F:09CB`). chrono-4 (after the Black Tyrano) loads onto the map after the fall and plays neither |
| 12,000 B.C. (`$1F6`, scene 6) | 0.48 | zophar chrono-6 slot 2: `$C2:7BD8`-`$7D5E`, called from scripts at `$7F:068D`-`$06A0`, `$0853`-`$0887`, `$0C0D`, `$0C86` |
| 12,000 B.C. (`$1F4`, scene 4) | 0.25 | ctsave08 slot 2; ct-state7, ct-state8: `$C6:E170`, `$C6:E694` (six calls at `$7F:13D3`-`$140B`), `$C6:E1FF` |
| 1999 A.D. (`$1F7`, scene 7) | 0.16 | none; the bucket ending and the attract demo pass over it: `$C2:7A56`, `$7AAF`, `$7ADA`, `$7AEB` |
| 12,000 B.C. (`$1F5`, scene 5) | 0.12 | zophar chrono-5 (all three slots): `$C6:E5C5` |
| 1000 A.D. (`$1F0`, scene 0) | 0.10 | every 1000 A.D. save: `$C2:48CD`, `$C2:27DE`, `$C2:7B4F` |
| 600 A.D. (`$1F1`, scene 1) | 0.07 | `$C2:7911`'s first routine |
| shared by several maps | 0.97 | the overworld's routines several scenes start: `$C2:3AE2` (`overworld_follow`), `$C2:2F94` (the region's song), `$C2:3264` (`overworld_watch_the_ground`), `$C2:3404` (`overworld_lead`), `$C2:42DD`, `$C2:309E` (the vehicles) |
| the object commands themselves | 2.63 | handlers whose paths no scene's script reaches in play (`overworld_epoch` 0.33, the irises `$C6:E368`/`$E498`, `show_tile_group`, `move_to_across`/`_down`, `scroll_map_plane`, `drift`, the window lines) |

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

KiB re-measured with the clusters; the locations are the scripts' (`tools/disasm/derive.py`'s
`event_scripts`, the location records' script word).

| Command | KiB | Locations whose scripts use it |
|---|--:|---|
| `$98` move toward object, counted | 0.19 | `$008`, `$192`, `$100` |
| `$9A` move to tile, counted | 0.15 | `$1A3`-`$1A7`, `$195` |
| `$9E` move toward object, keep facing | 0.13 | `$0CC`, `$1A3`-`$1A7`, `$029` |
| `$D6` take out of the party | 0.09 | `$0E6` |
| `$9D` move by variables, keep facing | 0.08 | `$005` |
| `$28` if well on screen | 0.08 | `$16B` |
| `$14`/`$15`/`$13` compare variables, words | 0.23 | `$005`, `$0DF`/`$0E1` (the race), `$1B2`, `$08F`, `$195`-`$1A1` |
| `$5E`, `$61`, `$60`, `$72`, `$76`, `$50`, `$52`, `$54`, `$59`, `$4D` word arithmetic | 0.41 | `$005`, `$1B2`, `$08F`, `$0DF`/`$0E1`, `$172`-`$17B`, `$199` |
| `$63`/`$64`/`$69`/`$6B` flags by number | 0.16 | `$1B2`, `$0DF`/`$0E1`, `$066`-`$06B`, `$086`/`$087`, `$1D1` |
| `$C4` ask (bottom window) | 0.05 | `$0F2`, `$0F3`, `$13B`, `$07A`, `$1D1` |
| `$D7`, `$C7`, `$CD`, `$CB` items and gold | 0.14 | `$115`; `$175`-`$1BC`; `$1B7`, `$032`, `$09A` |
| `$DC` location put aside | 0.04 | `$0E4`, `$0E5` |
| `$7B` launch, `$5D` add variable | 0.12 | no event script uses them |
| touching an object (`event_touch_contact` `$C0:31B9`), `say` `$C0:2E1E`, sending a member to the reserve `$C0:3967`, `if in view`, the pad tests (`if_pressed_*`, `if_held_*`) | 0.48 | the touch's other paths and the minigames' pad tests; locations not mapped |

Looked into for the second batch (Targets, 11): the fair's locations (`$005` the lower square,
`$008` the telepod exhibit, `$1B2` Norstein Bekkler's tent, `$1B7` the bell plaza) hold several of
these, as does Spekkio's room (`$1D1`, the bottom window's question, which
`battle-tech-sweep-spekkio` reaches).

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
- **Movie**: `battle-tech-sweep` (route `tech_sweep`, 118090 frames, the Core beaten): `tools/movies/routes/tech_sweep.py` (zophar chrono-trigger slot 1:
  Crono, Magus and Marle inside Lavos's shell): walks to the second Lavos form, fights it and then the
  Lavos Core with a policy (`tools/movies/policies.tech_sweep`) that opens Tech on each member's turn and
  takes the first list entry -- by tech id, read from the tech-by-id table at $94D0, not list position,
  so a double tech is found once whichever partner's turn reaches it first -- that isn't cast yet,
  falling back to Attack once a member's list is exhausted for now (not enough MP, or a tech whose only
  target needs an ally down); heals and revives with items. Casts 20 single techs (ids 1-5, 9-15,
  49-56) and no double tech (below), each once, well inside the first third of the
  fight; no triple tech appears for this trio. Its recording, cut to the shortest length (found by
  bisecting the `--coverage` cutoff) that still reaches everything it reaches -- the Lavos Core down to
  its last few hits, not beaten outright, since that's as early as two of its addresses (inside
  `battle_select_random_member`, an enemy target selector) are reached, an artifact of the Core's own AI
  nearing death rather than of the sweep's tech order -- is 140500 frames and would reach 0.84 KiB (406
  addresses) no other movie runs, in 28 routines, mostly in `$C1`: the three largest are
  `battle_work_out_enemy_attack`, `script_point_along` and `battle_apply_damage` (uncommon branches
  inside routines every battle already reaches), plus a few reached for the first time outright,
  `battle_effect_anchor_chase` and `fight_apply_accessory_effects` among them. Recorded again whole once
  `script_return_from_call`'s guard took the return at the top's going on into it
  (history/reimplementation-chunks.md, "Phase 4a, the shipping mode's fallbacks, chunk 9"): 118090 frames to the Core beaten, all 20 techs
  cast; shadow clean, no refusal, and in the library.
- **What the first sweep cast** (read again for the second): ids 1-5 are Crono's singles, 9-15
  Marle's and 49-56 Magus's -- every character's eight come first, Crono's at 1, and the double and
  triple techs from 57 on -- so it cast no double tech. The policy took the first entry not yet
  cast and tried it again every turn it was refused: Crono's sixth (a revive, with nobody down) and
  Marle's eighth stood in front of the rest of their lists, the double techs included.
- **Movie**: `battle-tech-sweep-spekkio` (route `tech_sweep_spekkio`, zophar chrono slot 1, 134646
  frames): all seven at the End of Time ($1D0), where Y on the field opens the exchange with the
  reserve (the gate's screen; from the main menu the exchange only reorders the active three).
  Spekkio's room ($1D1) is a door away; at these levels (58-63) his form has 10000 HP, attacks took
  nothing off it, and he gives the fight up once techs have worn him down far enough -- a dozen to
  thirty techs -- and he can be fought again. Lucca, Robo and Ayla fight him twice, then Crono,
  Marle and Frog twice, with `policies.tech_sweep` extended: a tech the bot couldn't take is skipped
  for the turn (`refused`, which fixes the first sweep's blocking), double and triple techs are
  tried first (`combinations_first`: they need every partner's turn, so a fight that ends early
  leaves them), and once the lists are swept the strongest tech cast so far finishes the fight
  (`finish_with_techs`, `damage`). 62 techs cast: Crono 1-5, 7, 8; Marle 9-16; Lucca 17-23; Robo
  25-30; Frog 33-40; Ayla 41-48; double and triple techs 57-59, 66-68, 78-80, 84, 85, 87-89, 93-95,
  104. Left: 6 (Crono's revive, nobody down at his turns), 24, 31, 32, 86. Reaches 1.92 KiB no
  other movie runs, all ported: battle screen scripts (`battle_screen_script_ripple_windows`,
  `battle_screen_open_band`, `..._orbit_middle`, `..._drift`, `..._orbit_position`,
  `battle_screen_random_byte`), `battle_draw_window`, the area searches (`$C1:2203`, `$22A4`,
  `$25A3`), `battle_effect_combatant_orbit`, `battle_effect_steal` (Ayla's Charm),
  `palette_lighten_red_blue`, `rewards_learn_combination_techs` (the second trio learns a triple
  tech at the last win), the bottom window's question at Spekkio's (`event_cmd_ask_bottom`) and a
  text code (`$C2:58B2`). The coverage cutoff is 126855, the rewards' learning; the movie keeps the
  field back in hand after it. Shadow: no mismatch, no `page refused`, no `accessor out of range`;
  **`script_jump_to_the_point`'s guard (`$C1:672E`) refused 208 calls**, from frame 108588 (the
  second trio's first fight; 6674 instructions run as the original). The route reaches its end in
  the shipping mode (`route_check --mode on:on:c`, frame 192208).
- **Movie**: `battle-menu-buttons` (route `battle_menu_buttons`, zophar chrono slot 1, 5086 frames;
  cutoff 4515): Spekkio's fight started as the sweep does, then at the first menu the buttons the
  bot never presses -- right and left to the other members waiting, the item list paged with L and
  R and left with B, across the tech list and out, X -- and the movie stops in the fight. It reaches
  0.36 KiB the 42 movies don't, 0.10 beyond the tech sweep's (`battle_take_menu_input`'s member
  switch and paging, `$C1:11AE`-`$120E`, `$C1:147B`-`$154A`), all ported. Folded into the sweep's
  last fight it made that fight 26000 frames longer (its choices went another way), so it stands
  alone. Shadow: no mismatch, refusal, `page refused` or `accessor out of range`; comparisons still
  open at the end, as for any movie stopped in a fight. The route reaches its end in the shipping
  mode (frame 4987).
- **Not recorded, why**: a third trio (Crono, Lucca and Frog next) would be some 70000 frames more
  for about 0.4 KiB, the second trio's share (0.46 KiB over 72000 frames, against the first's 1.46
  over 60000): the rest of the 5.94 KiB of animation commands isn't in the double techs between
  these characters. zophar chrono-5 (all but Magus maxed) stands on an isolated 12,000 B.C. floating
  island with no encounters: `ow_bfs` finds its whole reachable ground is 57 tiles with no door.

### 3. Menu sweep
- **Save**: ctsave14 slot 1 (seven members).
- **Do**: tech screen scrolled past three members and eight techs; equipment
  changed member to member from its slide; items: the tabs, the users list, an
  item used on the whole party; Settings, a custom button assignment; save from
  a save point (the save screen's own opening, `$C2:D4F8`).
- **Reaches**: up to 2.08 KiB (1.82 ported) of the menu clusters, about 0.4 KiB a minute.
- **Movie**: `menus-sweep` (route `menu_sweep`, 6424 frames; the `--coverage` cutoff is 6393, the
  last press out of the controls' rows): the item screen (a Shelter and a Lapis tried on the party
  -- refused, as this save's party is at full health, after the member window opens with a cursor
  on every member -- the first weapon's users, the list paged with L down to a Power Tab used on a
  member after looking at three), the techs screen (the members slid past three rows down and back,
  Crono's double techs scrolled past eight rows, paged with R and L), the equipment screen (the
  members slid to a reserve member, a weapon changed, R/L switching member from the slots) and
  Settings (the window design stepped along its meter and back, the controls set to the player's own
  with two assignments stepped, Start putting the game's buttons back). It reaches 2.01 KiB of live
  code no other movie runs, 1.67 ported (`menu_slide_tech_members`, `menu_slide_tech_list`,
  `menu_point_at_tab_member`, `menu_show_item_users`, `menu_put_cursors_on_every_member`,
  `menu_step_setting_meter`, `menu_use_tab`, `menu_turn_page`, `menu_switch_equipment_member`,
  `menu_choose_buttons`) and 0.34 unported (the equipment members' slide `$C2:9D87`-`$9E75`, the
  button rows `$C2:C20F`-`$C296` and `$C29D`, the window design's `$C2:C5C7`-`$C620`).
  Shadow found a port bug, fixed in chunk 9: `menu_slide_tech_list` (`$C2:B72F`) left the
  sign clear in the P its tick saves (`$7E:0A78`, `$20` where the original has `$A0`). Of the 0.34
  unported, the equipment members' slide start is a port now, and the button rows and the window
  design were C already (`menu/settings_screen.c`), uncredited by the burndown until its reading of
  the menus' bridge was fixed (chunk 9). No refusals, no `accessor out of range`. The route reaches its end in the shipping mode (`route_check --mode on:on:c`, frame
  6461).
- **Movie**: `menus-save-point` (route `save_point`, ctsave12 slot 2, 3270 frames; cutoff 3219): the
  save screen a save point opens on its own (menu screen 11, `$C2:D4F8`), which no other movie
  opens -- `menus-end-of-time` saves through the main menu's Save while standing on the same save
  point. The End of Time's save point is its script's object `$0D` at tile `$18`,`$16`: standing on
  it, confirm asks the field for the menus' mode `$40` (event `$C8 $40`, which 35 locations' save
  points use). The route answers no to saving over the first slot, then saves over the third. It
  reaches 53 bytes no other movie runs, all ported (`menu_open_save_screen`, the question's no in
  `menu_answer_whether_to_save_over`). Shadow clean (no mismatch, refusal or `accessor out of
  range`); the route reaches its end in the shipping mode (frame 3250).
- **Not reachable**: the exchange's reserve scroll (`$C2:C85B`-`$C2:C972`, 280 bytes, unported), the
  ledger's "party exchange's panel". It runs only where the reserve holds more than the four rows
  shown (`menu_exchange_scrolls`, `can_scroll`), and the reserve is the party's record places 3-8
  that hold a character: with seven characters and three in the active party it holds four at most.
  The gate's screen (`menus-end-of-time`, and the seven-member ctsave12 slot 2) shows all four
  without scrolling. Nothing in normal play gives it a fifth; the step off a row the story keeps
  (`$C2:C9AE`) wants a story point that keeps a member in the party at the End of Time, not
  identified.
- **Route**: `menu_sweep`, `save_point`.

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
- **Movie**: `battle-black-tyrano` (route `black_tyrano`, 49219 frames; the `--coverage` cutoff is
  48663, the end kept at the field back in hand after the scene, since a movie stopped while the
  field loads leaves a comparison open): out of the save point's room through the hall (`$12A`) and
  the corridor (`$12C`, whose scene -- Azala looking down from the stairs -- starts past tile row
  20) into the throne room (`$12D`), where confirm at the throne's foot (its objects `$10`-`$14`
  call object `$15`'s function 3) starts Azala's challenge and the battle (formation `$93`: the
  Black Tyrano, 10500 HP, slot 3, and Azala, 2700, slot 7). The `party` policy reaches only for
  Full Tonics, Elixirs and Mega Elixirs, which this save hasn't, so the Tyrano's countdown flame
  wiped the party 296 times before a win; `policies.party_with_lapis` heals with what the bag holds
  (a Lapis when two are low, a Mid Tonic for one) and wins without a wipe, at frame 44589. Then
  Azala's last words and event `$FF $24`, the scene program's mode 4 (Lavos falling) from frame
  47469, and the field back at `$12D`. It reaches 0.80 KiB no other movie runs, 0.48 unported:
  mode 4 whole (`$7E:53BB`-`$5529`) and code beside it (`$7E:350E`, `$5855`, `$56EB`, `$55F9`,
  `$56A9`); ported, `script_point_along`'s branches (`$C1:7447`), field paths (`$C0:964D`, `$9890`,
  `$8285`) and the battle screen's framed wave. Shadow: no mismatch; `script_return_from_call`'s guard
  (`$C1:56FB`) refused 24 calls, the gap `tech_sweep` found, until chunk 9 took the return at the top's
  going on into it. Mode 4 is a port since (`scene/lavos_falls.c`; its pixels' spread `$7E:350E`
  excepted). No `page refused` or `accessor out of range`. The route reaches its end in the shipping
  mode (`route_check --mode on:on:c`: won without a wipe at frame 44718, mode 4 from 47598, the
  field back in hand at 52013).
- **Route**: `black_tyrano`.

### 7. The other endings
- **Saves**: technoplaza ct-state17 and ct-state19 to ct-state26 (New Game+ at each ending's story point).
- **Do**: beat Lavos at that point and watch the ending.
- **Measured**: the first eight scratch recordings below add no new `$7E` scene
  code and start none of modes 5, 6, 10/11/14/15 or 12. Reunion starts modes 11
  and 15, adding nine scene instruction bytes at `$7E:8CE9`-`$8CF1`; the rest of
  those paths already runs in the 48-movie library. The earlier 4.71 KiB
  estimate was an upper bound, not coverage these saves demonstrate.
  Mode 7's loss sequence and mode 6's Epoch dive already have separate movies
  (target 8). None of these nine routes starts modes 5, 10, 14 or 12.
- **Route**: yes (`bucket_lavos`, `telepod_lavos` and `beyond_time` are the pattern).
- **Hand-run routes**: these long recordings stay in scratch storage, outside `oracle/movies`.
  Their routes are in `tools/suite.py`'s `HAND_RUN_ROUTES`, excluded even from `--long-routes`;
  regenerate and check them explicitly (MOVIES.md). `tools/movies/other_endings.py` shares the
  Lavos fights and recording setup, and reuses `telepod_lavos.to_the_shell` for the fair.
- **The Successor of Guardia**: `successor_of_guardia`, technoplaza ct-state17 slot 2,
  129223 frames. Crono and Marle enter the right telepod, beat the shell (13145), inner Lavos
  (44875) and Core (117187), without a wipe, return to the fair, and speak to the royal guard
  below the fountain. The royal family's reveal at `$04F` leads to Frog and Leene at `$057`,
  their credits, heart and The End. Completion checks object 0's final halt at script offset
  `$0270` in `$057`, after its fade, then keeps another 120 frames; the final screen was inspected.
  Identification is from the save's RAM and the game: `$7F:0000 = $27`, and location `$052`'s
  script 9 (`$FD:9000`) selects `$27 <= story < $2D`, sets `$7F:006E = 5`, and returns to `$008`.
  That flag enables lower-square objects `$1B`/`$1C`, whose conversations start `$04F`.
  Against the coverage union of all 48 library movies, this recording adds 159 instruction
  bytes (0.16 KiB): party removal (`$C0:3967`-`$3992`, `$39E1`-`$39F3`), field paths,
  battle aiming (`$C1:C8CC`-`$C8E9`), and palette paths including darkening blue
  (`$FD:E6A1`-`$E6D0`). **No new `$7E` scene-program code**; none of modes 5, 6, 10/11/14/15
  or 12 starts in this ending. Shadow: 0 mismatched, 0 refusals, no `page refused`,
  `accessor out of range` or asset diagnostics; sound lockstep identical. `ct_native` and
  strict mode both run all 129223 frames and exit 0 without a stop. Fixed-input completion
  in those modes does not establish the ending's outcome when their timing drifts.
  Shipping-mode `route_check --mode on:on:c` passes at frame 176603, all fights without a wipe;
  its six restores away from a base replay exactly, and the C sound driver reports no dropped
  commands or samples placed over echo. No movie was added to the library, so the narrowed
  movie suite and its aggregate baselines do not apply. The route-check unit tests and the
  static don't-care, port-style, port-smell, source-effects and asset checks pass.
- **The Legendary Hero**: `legendary_hero`, technoplaza ct-state19 slot 2, 144640 frames.
  The bucket, Day of Lavos, shell with its boss imitations, inner Lavos and Core; all fights
  without a wipe (won at 56952, 85846 and 130160). The ending plays Robo and Atropos's reunion,
  Tata's journey to the throne, Crono, Marle and Lucca in place of the villains, and the
  interleaved staff credits through The End. The save's `$7F:0000 = $5D` selects
  `$54 <= story < $66` in `$052`, setting `$7F:01AB = 1` and entering `$025`. That ending flag
  advances through 6: `$025`, `$02C`, `$025`, `$02D`, `$025`, `$026`, `$025`, `$03C`, `$025`,
  `$02E`, `$025`. Completion requires flag 6 and object 0's final wait/jump loop at `$042B`,
  after the last fade-in; The End was inspected. Coverage against the 48 library movies adds
  **one byte**, the field-object loop's return at `$C0:A88C`, and no `$7E` scene-program code.
  None of modes 5, 6, 10/11/14/15 or 12 starts in this ending. Shadow exits 0: no mismatches,
  refusals, page/accessor/asset diagnostics; sound lockstep identical. `ct_native` stops at
  frame 14237, `$FD:B438`, "an enemy coming into the battle"; strict mode runs all 144640
  frames and exits 0 without a stop. Shipping-mode `route_check --mode on:on:c` passes at
  frame 168451 without a wipe; all 12 restores replay exactly, with no C sound-driver errors.
- **The Unknown Past**: `unknown_past`, technoplaza ct-state20 slot 2, 160225 frames.
  The shared bucket route beats the shell (75692), inner Lavos (103155) and Core (144665).
  Inner Lavos needed 24 retries; the recording retains only the winning input. Story
  `$7F:0000 = $69` takes `$052`'s `$66 <= story < $75` branch, sets `$7F:01AA = 1`, and enters
  `$025`. Credits alternate with `$024`, `$11A`, `$03F`, `$026`, and `$02B`, returning to `$025`
  after each. The last interlude has flag 6: object 0 waits at `$03F2` for object 8's Robo dance,
  which reveals The End and loops at `$04A9`. The route checks both objects and keeps another
  120 frames; the final picture was inspected. Coverage adds 92 instruction bytes over the
  48-movie library, in field/battle/menu paths (`$C0:BDE7` onward, `$C1:1574`-`$15E6`,
  `$C1:C8CC`-`$C8E9`, `$C2:8CF6`), **none in the `$7E` scene program**. This ending starts none
  of modes 5, 6, 10/11/14/15 or 12. Shadow exits 0 with zero mismatches and refusals, no
  page/accessor/asset diagnostics and identical sound lockstep. `ct_native` and strict mode
  both run all 160225 input frames and exit 0 without a stop. Shipping-mode
  `route_check --mode on:on:c` reaches The End at frame 146361; all 12 restores replay exactly,
  with no C sound-driver errors.
- **The People of the Times**: `people_of_the_times`, technoplaza ct-state21 slot 2,
  160570 frames. Story `$7F:0000 = $7E` takes `$052`'s `$75 <= story < $84` branch to `$05B`.
  The route beats the shell (59176), inner Lavos (85378) and Core (149750), without a wipe,
  then follows the character
  credits through `$05B`, `$05C`, `$05D`, `$05E`, `$05F` and The End. It requires object 1's
  final loop at `$035C` in `$05F` and another 120 frames; the final picture was inspected.
  The default Core seed is 2, the other fights' 1: with Core seed 1, the reference stalled
  before the reveal, object 1 at `$033D`, CPU waiting for the sound command echo at `$C7:0635`.
  Another 9000 frames and button probes did not release it. Replaying from the Core checkpoint
  with seed 2 completed the ending; neither run used pokes. The sound stall is in TODO.md.
  Coverage adds 16 instruction bytes over the 48-movie library (`$C0:CFEE`-`$D0A8`),
  **no new `$7E` scene-program code**. None of modes 5, 6, 10/11/14/15 or 12 starts.
  Shadow exits 0 with zero mismatches and refusals, no page/accessor/asset diagnostics,
  and identical sound lockstep. `ct_native` and strict mode run all 160570 input frames
  and exit 0 without a stop. Shipping-mode `route_check --mode on:on:c` reaches The End at
  frame 146552; all 12 restores replay exactly, with no C sound-driver errors.
- **The Oath**: `the_oath`, technoplaza ct-state22 slot 2, 159243 frames. The shared bucket
  route beats the shell (65635), inner Lavos (99411) and Core (134683); the Core needed
  18 retries, of which only the winning input is kept. Story `$7F:0000 = $87` takes `$052`'s
  `$84 <= story < $8A` branch, setting `$7F:00DF` bit 0 and entering `$159`. Frog's farewell,
  journey through Magus's castle and duel alternate with `$1CC`'s credits: `$159`, `$1CC`,
  `$0A5`, `$1CC`, `$0A9`, `$1CC`, `$0AD`, `$1CC`, `$0B0`, `$1CC`, `$0AE`, `$1CC`, `$1BF`,
  `$1CC`, `$0AC`, `$1CC`, `$0A5`. Completion requires `$E0 = 8`, `$DF` bit 1, and object
  `$0D`'s halt at `$0579` in the last castle scene. It keeps another 600 frames for The End's
  palette fade (32 steps at 15 frames each) and a final hold; the final picture was inspected.
  This route exposed a bot-table error: Frog's healing entries were Water 2 and Heal.
  The names at `$CC:15C4`, tech records at `$CC:1BEB` and effect costs at `$CC:253B`
  identify Heal as entry 3 (2 MP) and Cure 2 as entry 6 (5 MP); `policies.py` now uses them.
  Its shared `party_lavos_core` policy preserves healing but clears the left bit with physical
  attacks before using area techs, then targets the body and exposed Core. It uses Robo's
  learned Shock (entry 7, 17 MP), which reaches both, and restores MP for these windows.
  No game code changed. Shipping-mode `route_check --mode on:on:c` reaches The End at 161581:
  shell 70794, inner Lavos 103021, Core 137441, each without a wipe in the retained play.
  The final check resumed the Core checkpoint after adjusting only the Core policy; that
  restore replayed exactly, with no C sound-driver errors. The preceding full run had already
  played the same shell and inner fights with Frog's corrected healing table.
  Shadow exits 0 with zero mismatches and refusals, no page/accessor/asset diagnostics,
  and identical sound lockstep (499496 ticks, 25 loads). Coverage adds 181 instruction
  bytes over the 48-movie library, including `$C0:AC48`-`$AC68`, battle paths in `$C1`
  and `$CD:1E27`-`$1E33`, **no new `$7E` scene-program code**. None of modes 5, 6,
  10/11/14/15 or 12 starts. `ct_native` and strict mode complete all 159243 input
  frames with exit 0 and no stop.
- **Dino Age**: `dino_age`, technoplaza ct-state23 slot 2, 176261 frames. Story
  `$7F:0000 = $8E` takes `$052`'s `$8A <= story < $99` branch, sets `$7F:015C = 1`
  and enters `$10F`. The shared bucket route wins the shell (103936), inner Lavos
  (133140) and Core (160761), all without a wipe. Credits and Reptite/character
  vignettes alternate through `$10F`, `$002`, `$10F`, `$001`, `$10F`, `$00C`,
  `$10F`, `$0FC`, `$10F`, `$10E`, `$10F`. Completion requires `$015C = 7` and
  object 7's final song loop at `$03FD`, after Ayla's fall and The End's reveal,
  plus 120 frames; the final picture was inspected. A first attempt stopped making
  progress after a failed Attack at frame 75811: the item list stayed open, Lavos
  had 637 HP, and the bot waited for the command menu. A saved probe at 80000
  confirmed this. `Bot.do` now backs out of an unexpected list after a failed
  Attack, matching its existing Tech/Item recovery; the same shell replay finishes.
  The 39 movie-tool and route-check tests pass.
  Coverage adds 31 instruction bytes over the 48-movie library, in `$C0`, `$C1`
  and `$CD`, **no new `$7E` scene-program code**. None of modes 5, 6, 10/11/14/15
  or 12 starts. Shadow exits 0 with zero mismatches and refusals, no page/accessor/asset
  diagnostics, and identical sound lockstep (553865 ticks, 24 loads). Core seed 2 is
  the default. Seed 1 finishes at 193389 but has one shadow mismatch in `battle_irq`
  (`$D1:FE97`): at the wait, PPU `$210F` is `FF` in the original and `00` in native
  code, also at 15 later passes before the original leaves. That failure reproduces
  from the Core checkpoint with generated comparisons off; TODO.md keeps it open.
  `ct_native` stops at frame 15100 in `$FD:B438`, an enemy coming into the battle;
  strict mode completes all 176261 frames and exits 0. TODO.md records the native stop too.
  Shipping-mode `route_check --mode on:on:c` reaches The End at 142332 without a
  wipe; all 12 restores replay exactly, with no C sound-driver errors.
- **What the Prophet Seeks**: `what_the_prophet_seeks`, technoplaza ct-state24 slot 2,
  131287 frames. Story `$7F:0000 = $9F` selects `$052`'s `$99 <= story < $D4`
  branch, outside Memory Lane's `$A2 <= story < $AB` range: `$F7` bit 7 and `$190`.
  The shared bucket route beats the shell (69092), inner Lavos (95529) and Core
  (115627), without a wipe. Vignettes and credits run through `$190`, `$0CB`,
  `$14E`, `$09B`, `$192`, `$194`, `$18E`, `$1A0`, `$18F`, `$19E`, `$190`.
  The first `$190` pass sets `$F8` bit 7; `$19E` sets bit 6 to select the last pass.
  Completion checks both bits cleared and object 0 halted at `$01B6` in `$190`,
  then keeps 120 more frames. The final The End picture was inspected.
  Shadow exits 0 with zero mismatches, no page/accessor/asset diagnostics, and
  identical sound lockstep (411801 ticks, 24 loads). Two guards refuse three calls
  each at frames 119033-119035 in `$190`: `event_cmd_walk_with_the_party` (`$C0:3002`)
  and `field_second_member_follows` (`$C0:A2CE`). The second party link is `$80`,
  empty; their `gap` allowlist entries and RAM evidence are in TODO.md's Refusals
  in real play. Coverage adds 377 instruction bytes over the 48-movie library,
  **no new `$7E` scene-program code**; none of modes 5, 6, 10/11/14/15 or 12 starts.
  The first check also exposed a missing dialog resource: `$D8:D000` entry 77,
  `$D8:DD14`-`$DD25`, is a real final line, not a sentinel. Correcting the table's
  count to 78 and rebuilding the catalogue removes that diagnostic; all previous
  resources retain their ranges. The catalogue check (6092 resources) and all 42
  asset/movie-tool/route-check tests pass. No game routine was ported.
  `ct_native` and strict mode complete all 131287 input frames with exit 0 and
  no stop. Shipping-mode `route_check --mode on:on:c` reaches The End at 140017
  without a wipe; all 12 restores replay exactly, with no C sound-driver errors.
- **Memory Lane**: `memory_lane`, technoplaza ct-state25 slot 2, 167063 frames.
  Story `$7F:0000 = $A2` selects `$052`'s `$A2 <= story < $AB` branch to `$0EF`.
  The shared bucket route beats the shell (94053), inner Lavos (122829) and Core
  (157883), without a wipe. Marle and Lucca review the men, Crono interrupts,
  and The End appears. Completion requires `$0EF` object `$0E`'s loop at `$059E`,
  after its call-and-wait for the final reveal, plus 120 frames; the final picture
  was inspected.
  Shadow exits 0 with zero mismatches, no page/accessor/asset diagnostics and identical
  sound lockstep (525128 ticks, 24 loads). Seven `field_walk_the_leader` (`$C0:881E`)
  refusals at frames 166496-166502 are a `gap` in the allowlist and TODO.md: the
  ending's upward exit carries the leader through Y `$FFxx`, which its guard rejects.
  Coverage adds 111 instruction bytes over the 48-movie library, **no new `$7E`
  scene-program code**; none of modes 5, 6, 10/11/14/15 or 12 starts.
  The final dialog at `$DE:C000` entry `$BA`, `$DE:E29D`-`$E2B0`, was missing from
  the catalogue. Correcting the table count to 187 fixes that diagnostic without
  changing any old resource range. The catalogue check (6093 resources) and all 42
  asset/movie-tool/route-check tests pass. `ct_native` and strict mode complete all
  167063 input frames with exit 0 and no stop.
  Shipping-mode `route_check --mode on:on:c` reaches The End at 127246 without a
  wipe; all 12 restores replay exactly, with no C sound-driver errors.
- **Reunion**: `reunion`, technoplaza ct-state26 slot 2, 123341 frames. Marle,
  Lucca and Robo enter the right telepod and beat the shell (16619), inner Lavos
  (54295) and Core (86129), without a wipe. The save layout at `$FF:FA7B` restores
  story `$D4`; `$052`'s dispatcher sees Crono absent and `$6D` bit 7 clear, sets
  `$6E = 4`, and selects `$054`. The route verifies the story after map loading
  restores bank `$7F`, then verifies `$6E = 4` on arriving at the End of Time.
  Gaspar wakes the companions; talking to Robo, Frog and Ayla brings the departure
  counter to four (the absent Magus counts himself in during setup). Crossing
  column `$13` starts the return to the Moonlight Parade. The route walks to the
  telepod farewells, returns to the bell, and speaks to the King from the north.
  `$051` calls mode 11 at frame 108801 (`$7E:0384 = $2B`), then the maps are
  `$02D/$105/$08E/$05A`. `$05A` calls mode 15 at 115441 (`$2F`), followed by
  `$056` and `$058`. Completion requires both observed modes and `$058` object 0's
  final halt at `$0052`, after The End's reveal, plus 120 frames; the picture was
  inspected. Modes 5, 6, 10, 14 and 12 do not start.
  Coverage adds 126 instruction bytes (64 new instruction starts) over all 48
  library movies. The **nine new `$7E` scene bytes**, at `$8CE9/$8CEC/$8CEF`,
  select mode 15's continuation script without another staff roll. Shadow exits 0
  with zero mismatches, no page/accessor/asset diagnostics and identical sound
  lockstep (387382 ticks, six loads). It reports eight guard refusals at three
  entries, described in TODO.md's Refusals in real play. `ct_native` and strict
  mode complete all 123341 input frames with exit 0 and no stop.
  The two movement refusals are the departure's Y `$FFxx`; three visibility
  and three palette-release refusals check an odd sharing link despite the
  script owning the palettes. All have `gap` reasons in the allowlist.
  Shipping-mode `route_check --mode on:on:c` reaches The End at 125438,
  with the shell, inner Lavos and Core beaten at 16744, 49078 and 88617 without
  a wipe. It observes modes 11 and 15, all 459 restores replay exactly, and
  the C sound driver reports no errors.

### 8. The scene program's modes 6, 7 and 5
- **Mode 6** (`$7E:78DD`): location `$1B0` is the party in the winged Epoch over Lavos (event script
  276, `$FA:8F4F`), which the 1999 A.D. world map's object command `05 B0 03 07 19` enters and, as
  recorded here, travelling to 1999 A.D. on the time dial goes straight to. Lucca asks "Are we
  REALLY going on?"; Fight! (object `$0A` function 6, setting variable `$0B`) with the Epoch's
  wings on (story bit `$7F:00BA` bit 7, set at `$179` after the Blackbird) calls object `$0B`
  function 3: "We must use the Epoch to break through!", the location set to Lavos's `$1DA` and
  event `$FF $26`. Without the wings Fight! goes to `$1D2`; Run away back to the 1999 A.D. map.
  The attract demo passes `$1B0` with story bit `$7F:0057` bit 2 set by the title's `$1AF`/`$1B1`,
  which sends it back to `$3F0` before any window.
- **Movie**: `events-epoch-lavos` (route `epoch_lavos`, ctsave14 slot 1, 7792 frames; cutoff 7072,
  the end stays at the Lavos field's first window because a movie stopped while the field is
  loading leaves shadow mode's comparison of `field_enter` open and fails): board the Epoch, the
  time dial one period up from 1000 A.D. (`$7E:0C52` 3 to 2), travel, Fight!, and the dive (mode 6
  from frame 5502 to about 7140). It reaches 1.57 KiB no other movie runs, 1.40 unported: mode 6's
  own code (`$7E:78DD`-`$7B9B`, `$7FC4`-`$80E0`, `$8205`-`$82AF`, `$82F5`-`$838A`, `$7BF2`-`$7C2F`,
  `$81C4`-`$81FA`, `$7F58`-`$7F7C`) and `$7E:38C3`; ported, `overworld_epoch`'s branch and bits of
  `$C3`. Shadow: no mismatch; three guards refused calls this play makes --
  `scene_build_perspective` (`$7E:357B`, 202 calls), `scene_animate_the_palettes` (`$7E:37FA`, 2) and
  `scene_setup_line_effects` (`$7E:36CC`, 2) -- until chunk 9 ported what they turned away (the dive's
  starts of the lines, the palettes' frame called by a channel's sequence, color math's control and
  layers by the line). The rest of mode 6 is still the program's (docs/TODO.md). No `page refused` or
  `accessor out of range`. The route reaches its end in the
  shipping mode (frame 7722).
- **Mode 7** (`$7E:6248`): location `$1DC` is the operators' room of the Day of Lavos (event script
  295, `$FA:E699`); its object `$01` runs `$FF $07` on the room's second visit (story bit
  `$7F:00AA` bit 5 set by the first). Only the world maps' object command `05 DC 03 07 07` (on
  `$1F7` and `$1F0`) leads there: the Day of Lavos after a loss to Lavos, which
  `battle-lavos-gameover` plays -- the mode is the globe behind "But... the future refused to
  change". Against the library that movie is in, 42 bytes of the mode are left (`$7E:62E5`, mode
  8's sequence script choice, which shares the entry; no event script uses `$FF $08`/`$28`/`$48`/
  `$68`, and `$7E:63D9`-`$6405`). No movie needed.
- **Mode 5** (`$7E:7092`): no way in found. `$C3:0000` stores its A in `$7E:0384` and unpacks the
  scene program (`$C3:1313` to `$7E:3000`), which takes the mode from `$0384` & `$1F` (table
  `$7E:3043`). Event `$FF` with operands `$00`-`$7F` reaches it with the operand as the mode
  (`$C0:3FE0`), and no event script uses `$05`, `$25`, `$45` or `$65`. The only other entry is
  `$C0:2649` (`LDA #$85`, `JML $C3:0000`), the end of `field_close_into_the_scene` (`$C0:260E`),
  the field's screen effect 11 in the table at `$C0:21EE`, keyed on `$7E:0139`; nothing found
  stores 11 there (bank `$C0`'s stores write `$01`-`$04`, `$06`, `$09`, `$0C`, `$0D`, its
  increments go 7 to 8 and 9 to 10, no long store, no script poke). An indexed store or block
  move into `$0139` isn't ruled out; read as dead code until one is found.

### 9. World maps
- **Movie**: `field-65m-bc-dactyl-flight` (route `dactyl_flight_65m_bc`, ctsave07 slot 3, 3992
  frames; cutoff 3911, the landing, the end kept at the party back on foot): `dactyl_65m_bc`'s
  boarding at Tyrano Lair, then the eight ways flown in turn (40 frames each; a diagonal moves the
  dactyl along one axis), confirm pressed where no dactyl lands (each spot tried on a machine copy
  first), the menus opened in flight and closed, the map view's button pressed (Y opens nothing on
  this map), and ground to land on found by the overworld search pressing confirm. It reaches 0.10
  KiB no other movie runs, all ported: the following dactyls' turns into the V (`$C2:528A`-`$531B`,
  `$C2:5374`, `$C2:53D3`) and the leader's turning and refused landing (`$C2:4DF0`-`$4EE1`). The
  rest of scene 3's dactyl code is their setting down in the air as the map starts with the party
  flying (`$C2:4D19`, `$C2:50F0`, `$C2:54B5`), which the menus don't do: the map comes back from them
  with the dactyls as they were. Shadow clean (no mismatch, refusal, `page refused` or `accessor out
  of range`); the route reaches its end in the shipping mode (frame 3992).
- **Not recorded, the 12,000 B.C. and the other scenes' code**: what is left of scenes 4, 6 and 7
  is cutscenes on the map: scene 4's script starts one of twelve scripts by the bits of
  `$7E:1BA8`-`$1BAC` (`$7F:0414`-`$044B`), scene 6's by `$1BA8` bit 3 and `$1BAD` bits 1-5 (its
  `$C2:7BD8`-`$7D5E` calls, at `$7F:0632` on, follow a redrawn map, a song and the tile streamer),
  and scene 3's far calls `$C6:E072`/`$E101` are in its cutscenes after `$7F:06F1`. Those bits are
  set by the story event that leaves for the map at that moment, so each is a play from a save at
  that story point, which none of the saves is: zophar chrono-4 (just after the Black Tyrano) loads
  onto the map after the fall and plays neither of scene 3's.

### 10. Floor switches, palette commands, screen effects, text codes (static research)
- **Floor switches** (`$C0:0D45`-`$C0:16DB`): a switch is BG1 map tiles whose bottom row has the
  step event (upper attribute bit 4), the bottom tile `$E6` (one by two), `$EC`/`$EE` (two by two,
  left/right column) or `$FA`/`$FC` (two by three); the press's path is per shape and column. Found
  by loading every location in turn from an End of Time state (diagnostic pokes of the next
  location and the field's request, `$7E:0112`/`$0117`) and reading BG1 (`$7E:3000`) and the upper
  attributes (`$7E:7000`), kept to the record's edges: such tiles in 52 locations. The ones near a
  save: Lucca's house `$004` (two by two at (7, 10), (2, 4); two by three at (10, 4)), Arris Dome
  `$0D7`-`$0D9`, `$0DD` (two by three), the Northern Ruins `$042`-`$045`, `$049` (two by three,
  one map with several records), `$0E5`-`$0E8` (two by three), `$100`, `$10C`, `$170`/`$171`/`$176`.
  The one by two is only on the title's `$1AF`/`$1B1` and `$1FE`. Executed before: the detection
  and the two by two's right column (`$C0:1014`-`$11C8`, the telepod ending).
- **Movie**: `field-floor-switch` (route `floor_switches`, ctsave14 slot 1, 3501 frames; cutoff 3260):
  into Lucca's house from the 1000 A.D. map and onto its two by two at (7, 10) from the left column,
  then the menus opened and closed, which sets the map's screen up afresh and presses the switch
  again from the steps the map remembers. Reaches 0.50 KiB no other movie runs, all ported: that
  column's press (`$C0:0E5F`-`$1013`) and the press again (`field_press_the_remembered_switches`
  `$C0:28E1`, `$C0:28F9`). Shadow clean; the route reaches its end in the shipping mode (frame 3521).
  The house's other two switches are on tiles no walk in the house reaches. Every two by three
  above is pressed from below in a diagnostic test (each location loaded by a poke, the party put
  beside the switch, then a walk search), the left column each time. Arris Dome's `$0D7` and `$0D9`
  aren't among the rooms the guardian route's `$0D6`, `$0D8`, `$0DD` and `$0DB` lead to; Geno Dome (`$100`,
  `$10C`) has no save near. The factory's are: its hall (`$0E4`) is the first door from ctsave03
  slot 2's spot on the 2300 A.D. map.
- **Movie**: `field-factory-switch` (route `factory_switch`, ctsave03 slot 2, 8387 frames; cutoff
  8252): into the factory's hall, the active party's order changed from the main menu's exchange,
  the hall's console checked (object `$08`: with Robo not leading, the security's robots attack;
  the `party` policy wins without a wipe), the left door (object `$09`, tile (4, 5), confirm then up)
  to the lab side (`$0E5`), and onto its two by three at (25, 15)-(26, 15), which the search steps
  onto from the right column. Reaches 1.27 KiB no other movie runs, all ported: the two by three's
  right column (`$C0:1454`-`$16DB`), `field_redraw_after_picture` (`$C0:EE8B`-`$EFD0`, as the door's
  scene hands the field back), `event_cmd_set_location_put_aside` (`$DC`, the door's), the main
  menu's exchange laid out for a reorder (`$C2:CC4F`-`$CD2B`), `$C2:6180`, the battle's cursors.
  Shadow clean (no mismatch, refusal, `page refused` or `accessor out of range`); the route reaches
  its end in the shipping mode (frame 7329). The lab side's other exits weren't found by a walk
  search, so `$0E6`-`$0E8`'s switches stay unpressed.
- **Movie**: `field-northern-ruins-switch` (route `northern_ruins_switch`, ctsave14 slot 2, 3811
  frames; cutoff 3666): the rooms at `$041`-`$049` are the Northern Ruins (one map with
  several records, the carpenter's and Cyrus's grave among them), whose entrance is a step from
  ctsave14 slot 2's spot on the 600 A.D. map. Into `$041`, on to `$043`, onto its two by three at
  (58, 38)-(59, 38) from the left column, then the menus opened and closed. It reaches 0.72 KiB
  beyond the batch's other movies (1.07 against the 42), all ported: the two by three's left column
  (`$C0:11C9`-`$1453`) and its press again (`$C0:297D`-`$29B9`). Shadow clean; the route reaches its
  end in the shipping mode (frame 3811). `$042`'s two by three is pressed from the left as well, 66
  steps in. Left of the floor switches: the one by two (only on the title's maps and `$1FE`).

- **Palette commands** (`palette_lighten_*`/`darken_*`): event commands `$2E` (the screen's colours)
  and `$88` (an object's) with a fade form, `$4n` lightening or `$5n` darkening, `n` the channels
  (`game/core/palette.c`'s tables: lightening 1 red, 2 green, 3 red and green, 4 blue; darkening 1
  green and blue, 3 blue, 4 red and green, 5 green, 6 red); pulses come from location sets and the
  battle, no event command. By the scripts: lighten green at `$185`, `$1A3` (`$2E`) and `$0F4`
  (Death Peak's wind slope, `$88`); lighten red and green at `$13C`, `$13F`, `$144`, `$145` (the
  Black Omen, `$88`); lighten blue at `$049`, `$188`-`$190`; darken blue at `$057`, `$09B`, `$0CB`,
  `$0CC`, `$0CE`, `$14E`, `$18E`, `$18F`, `$192`; darken red and green at `$004`, `$056`, `$0CD`,
  `$123`, `$18C`, `$1A3`, `$1A0`; darken green at `$0CC`; darken red at `$07A`. Left after this batch:
  `darken_red_blue` (no event script's form; the battle's), `lighten_green`, `darken_green`,
  `darken_red`, `darken_blue` and the pulses, 0.38 KiB. `battle-tech-sweep-spekkio` reaches
  `lighten_red_blue`, a battle script's.
- **Screen effects** (`$C0:2307`, effects 6-12 of `$C0:21EE`): event `$FF`'s modes `$95` (at `$01A`,
  `$1B6`, `$1B8`, `$1B9`: the Gate opened at the fair's telepod and its arrivals), `$96`, `$9B`, `$9E`
  (`$0AC`), `$99` (`$109`) and `$9A` (`$1AD`) are the ones no movie runs; `$9D` no script uses.
  `$95` is the telepod incident: tried from technoplaza ct-state16 slot 2 (New Game+ at the fair):
  talking to Lucca at the exhibit (`$008`, object `$0D`) runs Crono's demo and takes the story
  counter `$7F:0000` from 8 to `$0C`, and walking down below row 12 starts the next scene, but Marle's
  turn wasn't brought on in the time tried, so no movie.
- **Text control codes** (`$C2:5953`-`$5C77`): what is left is the 16- and 24-bit numbers
  (`$C2:5972`-`$59C7`) and the inserts' tails; of the field's dialogs only the jetbike race's
  scoreboard (`$0DF`/`$0E1`) shows a 16-bit number.

### 11. Event commands at the fair
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

### 12. The side quests
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

| Quest | Save before it | What it plays | Bosses |
|---|---|---|---|
| Ozzie's Fort | ctsave12 slot 2 (slot 3 after it, confirmed by flags) | `battle-ozzies-fort`: the fort and final trap | Flea Plus, Super Slash, Great Ozzie |
| Fiona's forest | ctsave12 slot 3 (ctsave13 slot 1 after it) | Robo's 400 years: the forest's replanting, the campfire | -- |
| Lucca's mother | routes/luccas-mother-before-campfire, slot 1 | `events-luccas-mother`: campfire, gate to 990 A.D., timed password, Lara saved and GreenDream received | -- |
| Fiona's forest | ctsave12 slot 3 (ctsave13 slot 1 after it) | `events-fionas-forest`: the sapling, Sunken Desert, Robo's 400 years, reunion and campfire | Retinite |
| Lucca's mother | after Fiona's forest | the gate to 990 A.D. and the password typed against the clock; `ct_drive`'s machine copies make the timed part a search | -- |
| The Rainbow Shell | ctsave13 slots 1-2 (slot 3 after it) | the Giant's Claw, the shell taken to Guardia Castle and the trial | Rust Tyrano |
| The Sun Stone | ctsave13 slot 3 (ctsave14 slot 1 after it) | `battle-son-of-sun`: the Sun Palace and its fight; `sun_stone` hand-run plants the Moon Stone in prehistory | Son of Sun |
| The Sun Stone | ctsave13 slot 3; route-written `sun-stone-planted` slot 1 | `sun_stone_save` plants the stone; `events-sun-stone` plays the theft, recovery, charging and rewards | Son of Sun in the preparation route |
| Cyrus's grave | ctsave14 slot 1 (slot 2 after it; `field-northern-ruins-switch` presses one switch) | the Northern Ruins and Cyrus's ghost, the Masamune's power restored | Hero's Grave guardians |
| Geno Dome | ctsave14 slot 2 (Targets, 5) | the conveyors and doors | Atropos XR, MotherBrain |

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
| The Rainbow Shell | ctsave13 slot 1 before Giant's Claw; slot 2 after shell delivery, before the trial; slot 3 after it | `battle-rust-tyrano` records the boss; shell delivery and the trial remain | Rust Tyrano |
| The Sun Stone | ctsave13 slot 3 (ctsave14 slot 1 after it) | the Sun Palace, the stone charged across the eras | Son of Sun |

The remaining quests are event-heavy plays the native program hasn't met: the event commands, scene work and
| The Rainbow Shell | ctsave13 slots 1-2 (slot 3 after it) | the Giant's Claw, the shell taken to Guardia Castle and the trial | Rust Tyrano, Yakra XIII |

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

### 13. The story events no movie plays

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

| Era | Event | Save before it | Boss |
|---|---|---|---|
| 12,000 B.C. | The Ocean Palace's end: `events-ocean-palace-lavos` | recorded ocean-palace-after-twins slot 3, made by `battle-golem-twins` from ctsave10 slot 3 | Lavos (the loss is scripted) |
| 2300 A.D. | Death Peak: the wind, the Time Egg, Crono brought back | ctsave11 slot 3, ctsave12 slot 1 | Lavos Spawn |
| 12,000 B.C. | The Ocean Palace's end: Lavos rises, Crono falls, Zeal's fall | after ctsave10 slot 3 (the Golem Twins) | Lavos (the loss is scripted) |
| 2300 A.D. | Death Peak: recorded in `events-death-peak-wind`, `events-death-peak-ascent`, `events-death-peak` (below) | ctsave11 slot 3, ctsave12 slot 1, route save | all three Lavos Spawn |
| 600 A.D. | The Cathedral, Queen Leene rescued | ctsave01 slot 2 | Yakra |
| 600 A.D. | The Cathedral, Queen Leene rescued | ctsave01 slot 2 | Yakra (`yakra` hand run, below) |
| 1000 A.D. | The trial | ctsave01 slot 3 is after it; a save before is to find | -- |
| 1000 A.D. | The castle's escape | ctsave02 slot 1 | Dragon Tank (`battle-dragon-tank`) |
| 1000 A.D. | The trial | routes/cronos-trial slot 1, made by `cronos_trial`; `events-cronos-trial` recorded | -- |
| 1000 A.D. | The castle's escape | ctsave02 slot 1 | Dragon Tank |
| 1000 A.D. | Heckran's Cave | ctsave03 slot 3, then the cave save point | Heckran (`battle-heckran`, below) |
| 2300 A.D. | Robo repaired at Proto Dome | between ctsave02 and ctsave03 | -- |
| 600 A.D. | Zenan Bridge | ctsave04 slot 1 | Zombor (`battle-zombor`) |
| 1000 A.D. | The castle's escape | ctsave02 slot 1 | Dragon Tank |
| 600 A.D. | Zenan Bridge | ctsave04 slot 1 | Zombor |
| 600 A.D. | Denadoro and the Masamune | ctsave04 slot 2 | Masa & Mune (`battle-denadoro` may hold the fight) |
| 65,000,000 B.C. | The Reptite Lair | ctsave05 slot 2 | Nizbel (hand run `battle-nizbel`) |
| 12,000 B.C. | Zeal's palace and the Mammon Machine | ctsave08 slots 2-3 | -- |
| 12,000 B.C. | Mt. Woe | ctsave09 slot 3 | Giga Gaia |
| 65,000,000 B.C. | The Reptite Lair | ctsave05 slot 2 | Nizbel |
| 12,000 B.C. | Zeal's palace and the Mammon Machine (`events-zeal-palace`, recorded below) | ctsave08 slot 2 | -- |
| 12,000 B.C. | Mt. Woe (`battle-giga-gaia`, recorded below) | ctsave09 slot 3 | Giga Gaia |
| 12,000 B.C. | Terra Cave | ctsave09 slot 2 | Mud Imp, Red and Blue Beasts (`battle-mud-imp`, below) |
| several | The Blackbird's end and the Epoch's wings | ctsave11 | Dalton Plus |

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

### 14. The original's known glitches
A faithful port reproduces the original's mistakes, so plays that lean on its documented glitches
test the ports where no ordinary play goes. The speedruns' inputs come from other emulators and
won't sync here; each is a `ct_drive` route from a save, written from the glitch's description
(chrono.wikidot.com/glitches; TASVideos submission 2240 and its forum thread).

| Glitch | What it does | Status |
|---|---|---|
| Unequipped armor or helm (US) | each empty slot adds 10 magic and 15 resistance to Lightning, Shadow and Water; two slots reduce incoming damage to about 12-14%, while outgoing damage depends on level and magic | `battle-unequipped-armor`, ctsave04 slot 2, 22859 frames: Marle empties both slots and beats Spekkio; Ice 244-246 versus 157-159 equipped, incoming Ice 15 versus 108 and Lightning 20 versus 171 (comparison on a machine copy; details below) |
| 98 slots of one kind of equipment | the equipment screen reaches memory far outside the inventory | waits on the native program's reads past a table's end (TODO.md, "The native program") |
| Item duplication | interrupt a save after item IDs change but before their quantities do; load the invalid slot with A+Up/Down to retain the mixed inventory | researched below: the TAS uses an external reset during saving, which `ct_drive` and `.ctm` cannot request; no `menus-item-duplication.ctm` recorded |
| New Game+ save corruption | a reset in the middle of a frame leaves a half-written value in the save: maxed stats, nearly any equipment | needs a reset mid-frame, which neither `ct_drive` nor `ct_native` models |

### 15. Where the native program stops (recorded)
- **What it was for**: not code no movie runs but states no movie reaches, which only
  `ct_native` and the hand ports alone show (TODO.md, "Refusals in real play"). The survey and
  the routes are in MOVIES.md ("Routes").
- **Movies**: `events-magic-cave` (route `magic_cave`, ctsave06 slot 1, 6156 frames) and
  `field-12kbc-after-the-fall` (`last_village_12kbc`, ctsave11 slot 2, 9269) stop there;
  `battle-denadoro` (`denadoro_fight`, ctsave04 slot 2, 9532) fights monsters no other movie
  fights.
- **Code**: little that the 48 movies' union didn't run (each one's `--coverage` against it,
  ROM banks): 0 instructions for the magic cave, 7 in `$C2` for the walk after the fall, 31
  (`$C1` 25) for Denadoro's fight. The scenes' scripts and the monsters' AI are data, which the
  instruction count doesn't see.
- **Left out**: a fight inside Magus's castle (ctsave06 slot 3; 13 instructions more), whose way
  from the hall into the next room the route's search finds only at some frames (MOVIES.md,
  "Routes").
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
**Item duplication research (no movie).** [Submission 2240](https://tasvideos.org/2240S),
"Item duplication", describes three save-corruption methods. The save spans two frames;
resetting after the first leaves a prefix of the new inventory and a suffix of the old one.
Method 1 replaces IDs while retaining old counts (the run turns one Revive and twenty Tonics
into twenty Revives and one Tonic). Method 2 stops within the ID array, leaving the same item
in separate slots. Each takes one reset. Method 3 uses three resets to extend the duplicated
slots; repeated use supplies the equipment-screen exploit's 98 slots. A+Up/Down on the load
screen selects the invalid save before the cursor skips it. The
[forum's reset discussion](https://tasvideos.org/Forum/Topics/8001?CurrentPage=5&Highlight=198431)
distinguishes this run's frame-boundary resets from the finer reset timing possible on a
console. This is a reset **during a save**, not evidence that submission 2240 needs a
mid-frame reset. [Chrono Speedruns' glitches page](https://chrono.wikidot.com/glitches)
mentions mid-frame reset for overworld-position corruption but gives no item-duplication recipe.

The menu code and the original, inspected with `tools.disasm.linear`, establish the following:

- **Save and load:** `game/menu/saves.c`, `$FF:F9FB`, copies the saved RAM regions in order
  through the block-move stub called at `$FF:FA44`; the checksum and slot marker are written
  afterwards (`$FF:FA58`, `$FF:FA5F`). Inventory IDs at `$7E:2400` and counts at `$7E:2500`
  are separate arrays, so an interrupted copy can break their pairing. `$FF:F9C4` checks the
  marker and checksum; `$FF:FAA3` loads without repeating that validation. In
  `game/menu/load_screen.c`, `$C2:E61B`, the cursor moves before confirm is tested; confirm
  branches past `$C2:E743`, whose `skip_empty_slots` normally avoids invalid slots, and loads
  the newly selected slot. This explains the simultaneous direction and confirm input.
- **Inventory operations:** `game/menu/item_screen.c`, `$C2:AAE3`-`$AAFC`, swaps both IDs
  and counts. Sort (`$C2:AB62`) aggregates repeated IDs into one slot per item, adding counts
  in eight bits before limiting them to 99. Sorting therefore collapses the separate slots
  needed for the equipment exploit. `game/menu/items.c`, `$C2:9988`, clears invalid IDs and
  zero-count entries when the menus open; it does not merge valid repeated IDs.
- **Equipment:** `game/menu/equipment.c`, `$C2:8953`, scans the first 242 inventory slots and
  appends every matching, wearable entry, including repeated IDs. The original stores IDs
  at `$7E:0F00+row` and counts at `$7E:0F62+row` (`$C2:8987`, `$C2:898D`), without a capacity
  check, then writes a zero ID terminator. These bases differ from the submission's informal
  buffer description: with 98 entries the terminator already overlaps the first count.
  The C guard `menu_list_what_fits_can_be_made` requires fewer than 98 entries, reserving room
  for that terminator. This is a static limit, not a newly observed real-play refusal.

**Recording blocker:** `dev/ct_drive.c` advances whole frames with controller input and has
no external reset command; `oracle/src/movie.c` records only controller buttons, with no reset
event. Its machine copies restore a whole machine, rather than rebooting it with partially
written SRAM. The game's L+R+Select+Start reset does exist (`game/core/input.c`, `$C2:851B`-
`$8523`), but is polled by the menus' vblank handler; it is not the host reset used by the TAS,
and an equivalent duplication through that input has not been established here. Stop at the
unsupported TAS reset operation: no fabricated corrupted save, route or movie, and no runtime
shadow, coverage, native/strict or narrowed-suite result to report. The findings above are
source and code research; no game code or validation baselines changed.

### Unknown
- **Mode 5** (2.05 KiB): nothing found that starts it (Targets, 8).
- **Field engine**, **battle other**, **enemy movement**, **sound driver** and
  **palette effects** (10.9 KiB together): no trigger identified; expect the
  targets above to reach some of them incidentally.

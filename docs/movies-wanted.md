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
| 4 (`$7E:53BB`) | 0.46 (ported) | event `$FF $24` at `$12D` (era 4, beside ctsave08 slot 1's `$12F`, Azala and the Black Tyrano); `battle-black-tyrano` (Targets, 6) |
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
| Mad Bat, Bug, Flea | 0.20 | condition with-status (`$C1:8F11`), other kind present |
| Blue Beast, Red Beast | 0.18 | at half HP, acted bit, die (`$C1:9CB3`, also Mud Imp, Retinite, Turret) |
| Alkaline, Shadow | 0.16 | within reach, is-enemy, nearest other enemy |
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

The fair's games now run (`events-fair-*`, history/movie-routes.md); its locations (`$005` the lower square,
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
  going on into it. Mode 4 is a port since (`scene/lavos_falls.c`, its pixels' spread
  too). No `page refused` or `accessor out of range`. The route reaches its end in the shipping
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

### 13. The story events no movie plays
The library now plays the story from the Cathedral to Lavos, the side quests and the fair's games
(their accounts are in history/movie-routes.md). Of the story's events with a save just before
them (oracle/saves/README.md), one is left:

| Era | Event | Save before it | Boss |
|---|---|---|---|
| 600 A.D. | Denadoro and the Masamune | ctsave04 slot 2 | Masa & Mune (`battle-denadoro` may hold the fight) |

### 14. The original's known glitches
A faithful port reproduces the original's mistakes, so plays that lean on its documented glitches
test the ports where no ordinary play goes. The speedruns' inputs come from other emulators and
won't sync here; each is a `ct_drive` route from a save, written from the glitch's description
(chrono.wikidot.com/glitches; TASVideos submission 2240 and its forum thread).

| Glitch | What it does | Status |
|---|---|---|
| 98 slots of one kind of equipment | the equipment screen reaches memory far outside the inventory | waits on the native program's reads past a table's end (TODO.md, "The native program") |
| Item duplication | interrupt a save after item IDs change but before their quantities do; load the invalid slot with A+Up/Down to retain the mixed inventory | researched below: the TAS uses an external reset during saving, which `ct_drive` and `.ctm` cannot request; no `menus-item-duplication.ctm` recorded |
| New Game+ save corruption | a reset in the middle of a frame leaves a half-written value in the save: maxed stats, nearly any equipment | needs a reset mid-frame, which neither `ct_drive` nor `ct_native` models |

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
### Unknown
- **Mode 5** (2.05 KiB): nothing found that starts it (Targets, 8).
- **Field engine**, **battle other**, **enemy movement**, **sound driver** and
  **palette effects** (10.9 KiB together): no trigger identified; expect the
  targets above to reach some of them incidentally.

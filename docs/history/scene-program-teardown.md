# The scene program before the engine continuation teardown

### The scene program's teardown

The sixth subsystem off the gate: the gate no longer compares the scene
program's own code, the code the game decompresses to `$7E:3000` for every
scene (`$7E:3000-$7E:3175` and `$7E:3177-$7E:31BE`): its start, its work RAM's
clear, the first screen it sets up, the sound put back to normal, the first frame's wait, its main loop and
its end. `game/attract/`, which held the main loop, is folded into
`game/scene/program.c`, where the loop's C for a program with no machine
(`scene_runs`) already was. What the program calls stays under the gate: the
boot (`$C3:0000`), the setups, the frame's work, the scenes' routines the loop
calls, the tasks and the scripts (the rest of `game/scene/`).

**What blocked it, and how each closed.**
- *The interrupted wait's carry hand-off.* Joining the start and the loop
  under the comparison met a carry the first frame's interrupt left live
  across the wait, which shadow mode compared with the original's. With the
  program retired no comparison crosses that boundary: shadow mode's
  reference play runs the cartridge's program from the boot's stop at
  `$7E:3000` (which declares A, X, Y, N, Z and the carry unread; liveness has
  the overflow read), and in the shipping mode the loop's continuations read
  the game's state, not the flags.
- *The scalings as C.* The loop's pass calls the endings' picture
  (`$7E:8930`), the Epoch's (`$7E:8278`) and the turned map (`$7E:7441`) as C,
  each under its registry row's contract (`native_hand_entry_at`: widths,
  arithmetic, guard), and the original's call where the contract turns the
  state away. Not the race's: its routine waits for the last racer's tiles
  between its racers (`$7E:5211`), a wait the machine can't yet wait out where
  a port stands, so its call stays the original's (tried as C, the attract
  demo's race went wrong at once; TODO.md, "The race's scaling from the scene
  program's main loop").
- *The restart by return.* The pass that finds the scene over ends the
  program as C -- the display stopped, the sound put back to normal, the game
  handed back to the field's start -- where the old pass stopped at `$7E:302A`
  for its entry: the machine enters the end 0 times, against 22 (4 by the
  pass's stop, 18 by the idle pass's return) over the five movies below.

**The preconditions, and how each was shown.**
- *Fully ported, no original code for it in real play.* The burndown left
  none of it unported; the shipping fallback baseline has no line of it (its
  `7E:3176` line is the empty routine the scenes' task lists call, which
  stays theirs).
- *No generated code touches its state.* The calls on record (the library's
  `--calls` logs) into its routines come only from the program itself: the
  start's own routine (`$7E:3000` to `$7E:3063`), the sound routine from the
  start and the end (`$7E:30F3`, `$7E:3038`), the frame wait from the first
  frame (`$7E:3010`). It owns no work RAM of its own: its cells are the
  scene's.
- *Its unreached rows covered.* Its one row (the frame pass's) held 0 bytes.
- *Its recorded outcomes cover the movies that protect it.* `attract-demo`,
  `title-start`, `events-epoch-lavos`, `events-jetbike-race`,
  `events-jetbike-race-won` and `endings-beyond-time` are in the gate's
  sample, with `game`, `sram`, `places` and `pictures` in strict mode and
  `ct_native`; every scene starts and ends through the program, and the
  start's modes, the loop's calls and the end are what those movies' scenes
  exercise.

**Deleted.** Every row the program had in `game_natives` (eleven: the start,
its going on after the clear and after the sound, the setup's entry, the first
frame, the frame wait's end, the work RAM's clear, the first screen
(`scene_init_video`, `$7E:30F9`, which only the start calls), the sound put
back to normal, the end and the hand-back), their wrappers and guards
(`scene_setup_present`, `scene_after_work_ram_clear_present`,
`scene_frame_wait_present`, `scene_clear_work_ram_present`,
`scene_sound_back_to_normal_present`, `scene_program_restart_present`,
`scene_init_video_present`), the
scaffolding (the clear's DMA page left in A's high byte, twice), and
`game/attract/`; the main loop's two continuations are new rows of
`game_held_waits`, `.retired`. Its rows in `docs/ports.tsv` (15),
`tools/fuzz_routines.toml` (18), `tools/uncompared_baseline.txt` (7) and the
isolated comparisons of `tools/uncompared_evidence.py` (6),
`tools/timing_exceptions.txt` (2), `tools/unreached_baseline.txt` (1),
`tools/portsmell_baseline.txt` (1) and `tools/fallback_baseline.txt` (2).

**What calls it now.** In the shipping mode (`m_runs_retired_as_c`):
- the boot (`scene/boot.c`, still gated) calls
  `scene_program_starts_on_the_machine`: the program's contract (8-bit A,
  16-bit index registers, its code in place, a mode the table has), then the
  start as C, timed as the start was (`NATIVE_TIME_AS_SIBLING`): the clear,
  the sound put back to normal (the C the scenes' tasks already send their
  commands with), the first screen, the setup (its port's C where its guard
  takes the state, its original otherwise) and the first frame, which holds
  the loop's wait;
- the held wait's continuations make every pass: `scene_program_waits`, the
  idle pass with nothing to call, and `scene_program_next_pass`, the call and,
  once a script has ended the scene, the end.

Elsewhere the boot stops at the program's start and the cartridge's program
runs, compared with nothing; the fallback measure doesn't count it there
(`dev/fallbacks.c`, `the_cartridges_scene_program`, while the loop's code is
in place), and the burndown leaves its runs out (`RETIRED_RUNS`).

**The guards are contracts.** The start's and the end's (the end's code in
place, 16-bit index registers, the field's location a number) are checked
where they run, and a state that breaks one is `program_breaks_a_contract` and goes
to the original. The continuations keep theirs, the runtime's contract for
running them: the loop's call and test in place on the scene's page, and
whether there is anything to do.

**Its time.** The routines whose rows went keep their calibrated estimates
(`game_kept_estimates`: the start 10,929, the clear 6,782, the first
screen 3,889, the sound 4,325, the end 1), which the C names; the pass keeps the frame pass's (3,125). In
shadow mode the setups, which the start ran inline, are their own entries
now, called by the cartridge's dispatch: the title's declares what its return
leaves unread (A, X, N, Z, carry), and four setups' loading stretches with the
display blanked, which `scene_program_start`'s exception covered (its worst,
190,868 clocks, is the planet's), carry it themselves. In the shipping mode
title-start plays as before, frame for frame (`tools/trace.py diff`: work RAM
but for the stack below S, VRAM, CGRAM, OAM, save RAM, the picture). The pass
reads the routine to call through its accessor, as the original's call reads
its operand, where the old pass peeked it uncounted, and the Epoch's scaling
is C without the original's call around it: a few clocks a pass, which move
where a scaling meets the frame's interrupt, and the scenes' random draws stir
in the beam's position, so the movies with a scaling part from the old
shipping mode during it (attract-demo for 44 frames of its race, from frame
8,707; events-jetbike-race-won from the race, frame 4,400, and
events-epoch-lavos from the dive, frame 5,554, to their ends) and three movies' strict pictures were
re-recorded, every game, save RAM and place kept (`tools/outcomes_exceptions.txt`).

**The measures.** Census over attract-demo, title-start, events-epoch-lavos,
events-jetbike-race-won and endings-beyond-time (shipping mode): the machine
entered the program's end 22 times before (4 by the pass's stop, 18 by the
idle pass's return) and 0 now, and the Epoch's scaling 94,866 times (the
pass's call through the machine) and 0 now, C inside the pass; the race's
scaling is still the pass's call, 632 times. Entries the machine enters 119
to 117, 10,131,646 times to 10,038,425; the held wait's runs 6,308,175 idle
and 95,499 passes before, 6,308,157 and 89,591 now. Frame ends with a routine
suspended, attract-demo driven a frame at a time in `ct_drive --native on`:
6,690 of 22,500 in 87 runs (the longest 717) before and after, the race's
calls among them. `tools/machine_joints.py`: registered entries 2,147 to
2,137, held waits' continuations 7 to 7, returns through the emulated stack
1,000 to 995, stops 95 to 97 (the boot's stop at the program's start, which
the bridge's continuation made before, and the start's and the end's on a
broken contract), scaffolding writes 2,009 to 2,007. `tools/burndown.py`:
bank `$7E`'s code 25.1K to 24.7K, its entries 240 to 230.

**What replaces the comparison.** The protecting movies' recorded outcomes,
in strict mode and `ct_native` (whose scenes, `scene_runs`, are the same C),
the vblank check over the shipping mode's video writes (the end's blank lands
at line 37 or 38 where the reference's is at 38, as it landed at 39 or 40
before), and title-start's frame-by-frame equality with the shipping mode
before the teardown.


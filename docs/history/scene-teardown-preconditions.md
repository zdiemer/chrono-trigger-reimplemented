# Scene teardown: initial inventory

- **Scene teardown preconditions.** The initial inventory of the remaining
  scene engine has 272 registry rows. Burndown reports all of its code ported
  (24.7 KiB of scene-program code, 3.4 KiB of scene data routines and 2.6 KiB
  of engine code), but the conservative walk against the existing 84-movie
  coverage union finds 3,823 unexecuted bytes in 67 entries. All 67 have
  recipes in `tools/fuzz_routines.toml`; replay them with matched coverage,
  then close the remaining paths before removing their comparisons. The
  scene is not yet in `tools/unreached.py`'s audited subsystems.
  Census over `attract-demo`, `title-start` and `events-jetbike-race-won`
  enters 15 scene rows by the machine, 55,452 times (36,880 calls, 11,451
  stops, 2,544 returns, 4,577 wait passes); 92 scene entries run only as C.
  The whole run enters 84 rows 316,513 times, with 814 entries only as C.
  The console stub stops at `scene_nmi` 11,435 times; that handler calls
  `scene_frame_work` as often, and the frame calls each task list 11,455
  times. Make these checked C hand-offs. The race's 635 scaling calls,
  1,270 gathering returns and 635 returns after each racer need a held
  upload wait with a checked continuation for each caller. The remaining
  calls are the curtain jump/sweep, unpacker, sound send and first script
  step; the boot has 16 stops and four returns from its external callers.
  The program's loop already runs through its held continuations
  (4,479,233 idle passes and 651 working passes), with no machine entry at
  its head or first frame. This confirms that `game/attract/`'s earlier
  obstacle was closed by the scene-program teardown, not left to the
  interpreter. Preserve that boundary through the race conversion.
  The existing full-library shipping movie and route fallback artifacts
  identify the empty task at `$7E:3176` (1,312 returns). The reference
  artifacts additionally contain eight instructions in the race's upload
  wait and gathering tail (`$7E:5211`, `$5213`, `$521D`, `$5220`); those
  are not shipping fallbacks. Make the empty task a checked call and the
  race's wait a held continuation; a zero-percent burndown alone does not
  establish that no original code runs.
  The source baseline from `tools/machine_joints.py` is 1,689 registry rows,
  69 held continuations, 917 stack returns, 91 stops, six call-outs, three
  registry lookups, eight machine time tags, 24 register escapes and 1,587
  scaffolding writes. Within `game/scene/` itself: 171 stack returns, ten
  stops, one registry lookup, six machine time tags, 16 register escapes,
  320 video calls and 203 scaffolding writes. Keep the menus' and battle's scene interfaces until
  those callers retire, and check their scratch readers before deletion.
  Replacement observations are the protected identities in
  `tools/outcomes_baseline.txt`, not the excepted differences. In both strict
  and native modes, `attract-demo` protects the saved game and places 0-5;
  `title-start` protects the saved game, pictures 1/1800 and places 0-2;
  `events-jetbike-race-won` protects the saved game, save RAM, pictures
  1/900/1200/2700/3000/3300/3600/7500/8083 and places 0-6;
  `events-epoch-lavos` protects save RAM, pictures 1/1800/2100/2400/4200
  and places 0-4 (strict additionally protects its saved game and place 5);
  `endings-beyond-time` protects pictures 1/1800/2100/2400/3900/4200 and
  places 0-4. Each mode's additional picture identities remain protected
  too. The race, Epoch and ending routes must also run on the native shape.


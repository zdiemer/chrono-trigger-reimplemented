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

### The scene engine's continuation boundaries

The race's main-loop scaling call now runs as C. `HOLD_SCENE_RACE_UPLOAD`
holds `$7E:5211` with the saved racer and main-loop return frames, so no
scaling invocation remains suspended while the previous tilemap uploads.
Once that upload finishes, `$7E:5215` gathers the picture, calls the checked
Crono or Johnny continuation, and either starts the next racer or holds
the completed main-loop pass at `$7E:3026`. Gathering and both racer
continuations retain their calibrated rates; settled work stays at the
operation's own rate. The checked boundaries retain decimal mode, widths,
page, bank and code-image guards.

The console calls scene interrupt handlers directly. The NMI calls frame
work, which calls both task lists; setup unpacking, first script steps,
sound sends and curtain calls also use their checked C forms. The field's
scene boot and curtain hand-offs use the same contracts. The empty task
recognizes its unchanged return image and preserves the list's argument
and saved-context result without executing the original return.
These are shipping-mode hand-offs. While the scene comparisons remain,
shadow mode keeps the frame, task-list and unpacker call boundaries: the
lists synchronize their frame's work there, and the resource load owns the
retired decompressor's existing estimation error. Inlining those boundaries
into the enclosing comparisons produced two Beyond Time state differences
and moved the unpacker's 333,201-clock error into its caller. Restoring the
comparison boundaries clears the state differences without a new exception.
The console likewise leaves the NMI's shadow entry isolated: inlining it
made every NMI comparison abandoned. Shipping calls settle the caller after
pushing the return frame and the callee before returning, just as the old
call-outs did. In particular, settling the field's curtain call before its
return-frame push advances the attract transition four frames and loses
protected picture 10500 and place 25; settling after the push restores both.
The held race wait changes only unprotected picture observations in
`attract-demo` and `events-jetbike-race-won`. An A/B replay restoring only
the old race hand-off reproduces their previous picture digests; their
complete saved-game, save-RAM and place observations are identical.
The two existing picture exceptions record that measured change, while
every protected picture identity remains required.

Over `attract-demo`, `title-start` and `events-jetbike-race-won`, the initial
census entered 15 scene rows by the machine 55,452 times: 36,880 calls,
11,451 stops, 2,544 returns and 4,577 wait passes. The prepared continuations
reduce all of those scene entries to zero. Across all subsystems the same
movies go from 84 machine-entered rows and 316,513 entries to 69 and
261,029, with C-only rows increasing from 814 to 827. The race holds 4,556
upload passes, gathers 1,270 pictures and completes 635 scaling passes.
A race save/load comparison replays one frame exactly and produces
identical work RAM after the next 60 frames, with no lost or inexact state.

The conservative scene inventory has 272 rows and 3,823 bytes absent from
the existing 84-movie coverage union. Fresh ordinary and targeted fuzz
comparisons cover 3,807 bytes. Three source-backed proofs cover the other
16: the logo's nonnegative scaled magnitude, the figure draw's impossible
compare fallthrough, and the staff roll's palette copier selecting only
the lower bank. The ledger retains these preconditions for retirement.
Input fixtures compare Start and reset chords, and a supplied vblank edge
lets the turning-map setup compare its post-upload work. Shared race
movement evidence names the same boost, drift and bump operations used by
both ride and coast; interpreted callees alone supply no coverage evidence.


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

### Comparison gaps before retirement

The broad `scene_program_start` fuzz fixture has 11 VRAM mismatches in 384
comparisons, with 16 call-out budget skips, identical in the pre-review and
review builds at seed 1 (400 iterations, modes 0-15). Isolate the setup mode
and its VRAM-read state; the first reported differences are in the copied
map at VRAM 0004 and 6018. The focused title refusal fixture and narrowed
startup movies pass. This pre-existing failure is not a successful fuzz
receipt and must remain separate from the 29 clean review fixtures.

The scene series still needs a complete passing sampled-gate receipt after
its final corrections. Its one full run passed all 37 shadow movies and
56 shipping/native-shape routes, but ended with timing, fallback,
uncompared, effects and evidence failures. The first four now pass focused
checks or an audit of that run's logs. The evidence runner's corrected
process budget passes all 84 movies, ending `dc_evidence: OK`. These checks
do not constitute a fresh complete gate.

The scene's whole-picture scalers need comparisons that survive interrupts.
In the 37-movie scene sample, both race-picture entries have all 1,142 calls
interrupted; each bitmap start, row and sampling entry has 4,310 interrupted
comparisons. The bitmap entries still have matching comparisons (1,846 at
the start and row, 850 at the sampler). Bounded isolated fuzzing compares
6,000 race pictures and 8,027 bitmap cases without a mismatch, but does not
close this movie coverage gap. The review's race-won replay independently
compares all 3,524 NMI handlers and 3,524 frame-work calls; this checks the
interrupt work, but not its interaction with native bitmap work interrupted
mid-picture. Add resumable comparison checkpoints or a synchronized interrupt
replay before removing the five bitmap/race exemptions. Keep that distinction
when extending the race upload-wait comparison described above.

Held sound waits have independent shadow comparisons with interrupts
enabled: the attract opening compares the complete music wait and ten retry
sends. Their combined-run exemptions remain because enclosing C loops own
the waits (the time warp's are no longer compared: REIMPLEMENTATION.md,
"The time warp's teardown"). The merge coordinator runs
the sampled gate after these review fixes; it was not rerun in this review.


# Plan

What to do next, in order. [TODO.md](TODO.md) lists every open follow-up; this
says which of them to work on and why the rest waits. Milestones themselves are
in [APPROACH.md](APPROACH.md), [GENERATED.md](GENERATED.md),
[REIMPLEMENTATION.md](REIMPLEMENTATION.md), [MATCHING.md](MATCHING.md)
and [PLATFORM.md](PLATFORM.md).

## The targets

Four, and every ordering decision here serves them:

1. **Native ports for Windows, macOS and Linux**: no emulation backend, no
   interpreted assembly, the game fully reimplemented in C.
2. **Experimental ports to era hardware** -- GBA, Wii, GameCube -- running this
   code natively, not under emulation.
3. **An experimental SNES ROM compiled from the reimplementation.**
4. **Divergence is accepted** where the game stays structurally the same.

### What that makes scaffolding

The emulator, generated C, shadow mode and oracle are validation scaffolding.
They leave the product as their callers retire. The platform interfaces and
host backends stay, along with the game's frame scheduler and timing estimates:
`ct_native` needs those to preserve the pace of play (PLATFORM.md, "Timing
without a beam, as built"). Refining emulated cycles beyond what validation
needs is not a target.

The game's main thread and eight menu tasks still use coroutines whose waits
are deep in their call stacks (`runtime/port/fiber.h`). A small target needs
smaller measured stacks or resumable tasks; a browser needs a stack-switching
backend or the same rewrite. Neither needs a new implementation of the
per-port fibers that exist only to suspend the emulated machine.

### What "done" means, measured

A port's presence is not proof of native execution or gameplay equivalence.
Use these measures together; keep run-specific counts in the milestone docs
and their ledgers, not as competing snapshots here.

| Measure | Done when |
|---|---|
| `tools/burndown.py`, `tools/unreached.py` | All live code is implemented; unreached paths have execution evidence or explicit safety arguments. |
| `tools/fallbacks.py` | No original code substitutes for hand ports, no unexplained refusal, and the build without scaffolding reaches the same route outcomes. |
| `tools/portstyle.py`, `tools/portsmell.py`, qualitative review | The game reads as source; the machine is confined to interfaces awaiting removal. |
| `tools/census.py`, `tools/machine_joints.py` | No game entry needs the registry, emulated stack, bus, register escapes or comparison scaffolding. |
| Asset catalogue and accessors | Every resource is loaded through the store; no transfer depends on a mapped cartridge. |
| State accessors | Game variables live in C structs, with defined aliases and bounds, rather than a work-RAM image. |
| `tools/strict_check.py`, `tools/native_check.py`, `tools/outcomes.py` | The library completes with protected saved games, save RAM, pictures and places; routes verify intended gameplay outcomes. |

Burndown credits inlined work and guarded hand-backs, so its near-zero
remainder does not establish that the machine can leave. The census names
which caller still requires an entry. Recorded outcomes distinguish reaching
a movie's frame limit from playing its intended route.

### Retiring the gate, deliberately

Byte-exact validation is a scaffold, and target 4 is the licence to take it
down: the game has to stay structurally the same, not byte-identical. So a
subsystem retires it **on purpose, in its own commit**, when it is fully ported
and no generated code touches its state any more: its storage moves out of
emulated work RAM, and shadow mode is replaced for that subsystem by the routes
and the gameplay tests. Until that point the gate holds, in full. Retiring it
early, or by accident, is the one failure this plan is most concerned with.

**A guard becomes an assertion when the gate retires.** While the emulator is
there, a port's guard refusing a state is harmless: the original runs it, so
the product stays exact, and the rewrites lean on that -- event's guards turned
away 4.2 million of the 7.3 million states its fuzzing drew, all of them states
no movie produces (per-entry shadow totals match the predecessor's on every
entry). A native build has no original to hand a refused call to. So when a
subsystem retires the gate, each of its guards becomes a claim that the game
never produces that state, and it is checked as one: over the routes and the
movie library first, then as an assertion in the build, where a refusal is a
bug to fix in the port rather than a call to pass on.

**Retiring the gate also switches the scaffolding off.** What a port writes only
for shadow mode or for the original's still-generated code goes through
`ORIGINAL_EFFECT` (docs/PORTING.md, "Scaffolding"), and the build already
compiles every port once without it (`ct_game_native_shape`). When a subsystem
retires the gate, its scaffolding is deleted in the same commit, and anything
the evidence found reading it from outside (event's scratch cells feed the
menus' wave phase; TODO.md) is decided first. A subsystem's `portstyle.py`
scaffolding count is how much of this is left to take down.

### Working order

**The order now: battle and menus retire, then the machine leaves.** Sound,
data, core services, warp, overworld, the scene program and attract, field,
events and the scene engine have retired their byte-exact comparisons.
Their retained contracts and compatibility interfaces are documented under
the subsystem teardown sections in REIMPLEMENTATION.md.

1. **Finish the battle and menus teardowns.** Each needs its unreached paths
   accounted for, recorded outcomes protected, and its callers' contracts
   preserved. This work is in progress on another branch.
2. **Finish the asset and state boundaries.** Replace the last cartridge
   transfers with store-backed resources, and move the game's variables into
   C structs behind the accessors. Resolve shared scratch, table overruns and
   save projections before changing storage. These can proceed per retired
   subsystem when they do not overlap the teardown work.
3. **Remove the remaining machine interfaces from the product.** Delete
   wrappers, registry dispatch and emulator dependencies once no caller needs
   them; make `ct_play` the native program.

The native time model and service-fed video backend have landed (PLATFORM.md,
"Timing without a beam, as built" and "Video from service calls, as built").
Their remaining timing debts, scene video escapes and missing outcome checks
are follow-ups in TODO.md, not new backend milestones. Investigate the first
playback divergence before changing a rate or re-recording an exception.

Independent branches use separate worktrees and a shared process budget.
Keep shared-file edits local to the subsystem so these tracks can merge.

## The validation gate

Nothing lands without it, and it is the same gate for every phase. It runs
over the gate's sample of the movie library (`tools/gate_movies.txt`, a greedy
cover of the library's code per frame played plus what each check needs) by
default, and over the whole library with `--all-movies`, run batched over
everything merged since its last run (it takes 8 hours and more), which is
where recalibration and baseline updates come from; "every movie" below is every movie run:

- every movie in `--native shadow` exits 0 with no `mismatched [1-9]` lines,
  no `page refused` lines (a guard that turned away a call on a direct
  page the map declares no instance for) and no `accessor out of range`
  lines (a port that reached past a variable the map declares, into
  whatever follows it in work RAM; both in DISASSEMBLY.md, "The accessors");
- `--native on --generated only` traces identically to `--native off`;
- every hand port's estimated time stays within the timing gate's limits over
  the shadow runs, or is on its exceptions list with a reason
  (`tools/timing_gate.py`; REIMPLEMENTATION.md, "The timing gate");
- no video write in the shipping mode lands on the picture where the
  reference's doesn't, unless it is an accepted exception
  (`tools/vblank_check.py`);
- the route scripts reach their outcomes in the shipping mode, with the C
  sound driver (`tools/movies/route_check.py`: the fast route every time, all
  of them with `--long-routes` on main once per timing/runtime merge batch);
- the C sound driver, over a short `ct_sound_fuzz` with the game's songs
  and with made-up ones, answers every command, places no sample where the
  echo writes and drops none, the native mixer makes the S-DSP's every frame,
  and each kind of run's sound is the digest `tools/sound_golden.txt` keeps
  (SOUND.md, "How it is checked"; nothing compares it with the original's
  any more);
- every don't-care declaration in `game/` is one liveness agrees with
  (`tools/dontcare_check.py`; REIMPLEMENTATION.md, "Before the bulk");
- no port transliterates the assembly where one didn't before
  (`tools/portstyle.py`, a ratchet against its baseline);
- no port's logic reads what some port writes only as scaffolding where
  none did before (`tools/effects_check.py`, a ratchet against its baseline;
  PORTING.md, "Scaffolding");
- no port carries one of the review rubric's mechanical smells where one
  didn't before (`tools/portsmell.py`, a ratchet against its baseline);
- the original's code runs in place of hand ports nowhere new, and no more of
  it where it did, over every movie's reference play (`tools/fallbacks.py
  --check`, a ratchet against its baseline; REIMPLEMENTATION.md, "The fallback
  measure");
- no hand port's guard refuses a call real play makes unless its allowlist
  line says why (`tools/fallbacks.py --refusals`; "Retiring the gate,
  deliberately", above);
- the build with the scaffolding compiled out runs every movie to its end and
  reaches the routes' outcomes in the shipping mode (`ct_dev_native_shape`,
  `ct_drive_native_shape`; REIMPLEMENTATION.md, "The native shape, run");
- strict mode and `ct_native` preserve their recorded saved-game, save-RAM,
  picture and place outcomes (`tools/outcomes.py`); a retired guard reports
  `contract broken:` rather than silently weakening the contract;
- `--misses` stays empty;
- `py -m unittest discover tests` passes and the build is warning-free on
  all four CI platforms.

## Phase 3a: what is left of the transcription backlog

The reimplementation bar and its worked examples are in PORTING.md,
"Writing a port"; the rewrites are in history/reimplementation-phase3a.md.
Keep the style and smell ratchets, typed accessors and qualitative review
while removing the remaining machine interfaces.

## Phase 3: M5 in bulk

The porting accounts and evidence are in history/reimplementation-waves.md,
history/reimplementation-chunks.md and REIMPLEMENTATION.md. The remaining
uncovered runs are guarded hand-backs and conservative stop accounting;
TODO.md, "Uncovered runs outside the scene program", records the domains to
settle. New work follows CLAUDE.md's reimplementation bar and preserves the
named state and asset seams.

### How a wave is run

One subsystem per worktree; local edits to shared registries and ledgers;
a sampled gate at each coherent commit; one independent qualitative review
of the accumulated branch before merge. The merge gate runs on the combined
tree, with long routes once per timing/runtime batch and whole-library
recalibration in its own batched ledger commit (CLAUDE.md; PORTING.md).

The battle and menus accounts below remain with their pending teardowns.

**`$C1`: the battle.** The bank is in: the enemies' scripts, the
target search, the random numbers, an enemy's turn, the statuses' timers,
every action's effect, a party member's action carried out, the setup's party
helpers with the inventory and gold service (`$C1:8003`), the fight's start
(`$C1:FA8B`) with the `$FD` routines it calls, the rewards of a won
battle (`$C1:F93E`, `$C1:F205`, `$C1:B4E9`, `$FD:B201`: the experience, the
levels, the techs, the items put back), all of which the main loop runs as C,
and the animation script language (chunks 9a and 9b: the command table in C,
the flow, values, poses, drawing, effects and colors; the places and facings,
every move, the grid walk, the sound commands and the screen effect), and
chunk 10: the enemies' movements (`$C1:3772`-`$C1:4021`), the party's turns as statuses, counters and called-off combinations decide
them (`$FD:AB30`, `$FD:AC6E`, `$FD:A8CE`-`$A95F`, `$C1:B3F9`, `$C1:B762`), the
demo's party, and the battle's end's bookkeeping; and the leftovers: the
battle's start from the field and its return there, the setup's last part,
the scheduler's animation step, an effect's next frame and a member that comes
back once; the player choosing (the menu's frame, its lists, the target
kinds and the area search); and an action shown (history/reimplementation-chunks.md, "Phase 3, battle:
an action shown"): the showing's step, the action's animation chosen and
loaded, the poses' looks, the battle's own frame step and step 3, and the
hits landing with a felled enemy's spoils. `python3 tools/burndown.py
--uncovered C1` (and `CF`, `CC`) leaves only jump stubs into ports, and bank
`$FD`'s battle routines are C too, the rewards' messages (`$FD:AD17`) and the
turns' work cleared (`$FD:A982`) the last (history/reimplementation-chunks.md, "Phase 4a,
timing: the battle frame work's remainders, chunk 2"). The
burndown counts the bank done, but the fallback measure shows what its ports
still hand back: with an action's showing C, the largest left are the
battle frame work's loads the showing calls -- the background's change
(`$CD:0018`, `$CD:0EBD` and its moves at `$CD:107D`-`$CD:12B8`, 29 million
instructions of the reference play) and the effects' graphics sets
(`$CD:0015`, `$CD:002A`), which wait on the vblank handler's sends -- then the
steps' routines the dispatch (`$C1:0003`, `battle/steps.c`) still calls out to
and the frame waits (`$C1:007E`, the timing service's; TODO.md, "What an
action's showing left at its edge").

**`$C2`: the menus.** In: the Epoch's two Mode 7 screens, the object scripts'
instruction set and the map they scroll, the load screen, the window command
language, the cursor and sounds, the typesetter, the menus' opening, the
characters in the records, the records' task, the frame's input, the
tilemap touches, the equipment screen, the items screen, the techs screen,
the settings screen (its options through a table, the buttons' assignment,
the meters, the gauge and the window's design, and the main menu's panel for
it), the shop (its tabs, the goods and the party's items for sale, the
panel of who could use an item and how their figures change, the quantity
and the deal), the party's services the field's scripts, the power-on and a
New Game+ call (the party's record, equipping, filling HP and MP), the
figures, the healing and its rising numbers, the inventory's add and take,
the paging, the records' sprites and colours, the held cursors, the list
slide, the description's typing, the main menu, the save screen's opening,
name entry and the battle mode's screen, the shop's steps on the party
(a member's slots and the list of what fits), and the party's exchange
(screens 5 and 7, the main menu's panel for it and the gate's song), the
objects that change the screen (the fades, the mosaic, the palette's loads
and fades, the tile streamer), the equipment screen's list of what fits,
what the scenes' scripts call, the sprite-object mode's start and the frame's
input from the other banks. The
world map walked on foot is its own subsystem, `game/overworld/` (the
leader, the followers, the ground and its places, the regions' music, the
place's sign and the main loop with its round trips to the menus and the
Epoch's screens), with the vehicles' controller and the Epoch flown over the
map and through time, and the world map's set up from its record's streams.
The dactyls' flight over the 65,000,000 B.C. map is C too (history/reimplementation-chunks.md
"Phase 4a, the shipping mode's fallbacks, chunk 5").

The field, events and both scene layers' retained interfaces are described
in REIMPLEMENTATION.md, "The field's teardown", "The events' teardown",
"The scene program's teardown" and "The scene engine's teardown". Their
remaining work removes interfaces with their callers rather than adding ports.

## Phase 4a: what still runs on emulation

The remaining work between a finished `game/` and a native build with nothing
under it is below. None is optional for target 1; the host service backends
already built are described in PLATFORM.md.

- **Remaining video escapes.** Battle, menus and scene must replace their
  SNES-specific transfers with named services as their gates retire. TODO.md
  lists each site and the contract it needs. The service-fed host renderer
  and its pixel check are described in PLATFORM.md.
- **The native program, and the registry taken down.** `game/core/program.c`
  boots the game and dispatches its main thread and handlers; `ct_native`
  runs it with host services (PLAYING.md, "The native program"). Remove each
  remaining machine boundary in this order:
  1. Replace calls through the registry with C calls that preserve the
     callee's full contract and timing scope. A direct call must still check
     widths, decimal mode, page, bank and code-image assumptions.
  2. Replace legacy driver boundaries with explicit continuations. A loop
     waiting across frames needs a held wait, not recursive C calls or a port
     suspended for a whole screen (RUNTIME.md, "Held waits"). Preserve task
     switching and saveable caller frames.
  3. Retire each subsystem's comparisons deliberately. Delete wrappers and
     scratch writes only when the last gated caller stops observing them;
     retain contract assertions and the time charged to callers. The teardown
     sections in REIMPLEMENTATION.md explain the retained machine forms,
     held continuations and calibrated time tags.
  4. Once no subsystem needs the machine, remove the registry, intercept,
     entry macros and emulator from what ships; `ct_play` becomes `ct_native`.

  The census identifies the caller keeping an entry alive. Native outcomes
  and state-driven routes establish how far its replacement actually plays;
  a movie reaching its frame limit is insufficient.

- **The code no movie reaches, checked before its subsystem retires.**
  Extend `tools/unreached.py` to the remaining 65816 subsystems: battle
  and menu. Compare the full library's coverage with their
  ported live reach, then fuzz the missing paths against the original before
  retiring their gates. Every remaining byte needs a passing comparison or
  an explicit safety argument in the ledger, with its retirement obligations
  in TODO.md. The sampled gate checks the full-library ledger; it cannot
  establish absence from movies it does not run.
- **The game's state as C, not a work-RAM image.** Even `ct_native` keeps the
  game's state as the 128 KiB byte image the accessors read and write, which
  is the original's memory layout carried into the product. When a subsystem
  retires the gate, its variables move out of the image into C structs behind
  the same accessors (generated from `re/ram.toml`, so the ports don't
  change), and what the original shared through one cell becomes a field one
  module owns. Saves are written from and read into the structs, and a
  cartridge save (`.srm`) still loads. Measured by the share of `re/ram.toml`'s
  variables still backed by the image; done when none is.
- **Validation after the teardown.** Before retiring each subsystem, fill
  its library coverage in the recorded outcomes (REIMPLEMENTATION.md,
  "Recorded outcomes"), resolve the differences it needs to rely on, and
  cite the protected movies and kinds in its teardown section. Preserve
  the saved-game projection when its storage moves out of work RAM; keep
  the routes, sound digests and guards as assertions. The last teardown
  must leave no unrecorded reference behaviour it needs to check.

**Assets: imported once from the user's ROM, then loaded by name.** The
ports reach ROM data through named accessors (`*_data.h`), every byte of it in
a resource of the catalogue (`re/assets.tsv`), and read it from a store the
driver supplies: `ct_play`'s is an asset pack it imports from the ROM once,
beside the saves (PLATFORM.md, "The assets, as built"). The same importer can
build the image for a target that can't afford the ROM or the decoding (GBA,
Wii, GameCube). What is left:

1. **The last cartridge reads outside the store.** The video service's
   transfers from a cartridge address (`menu/display.c`, `battle/uploads.c`)
   go by DMA from the bus. The data is in the catalogue, so the transfers
   can send the store's bytes (`PfData`'s host pointer), as the unpacker
   reads its streams through the store already (`rom_read8`). A build with
   no emulator ("The native program", above) needs it.
2. **Decoded next, a resource at a time.** Decode the raw pack into structured
   resources: streams decompressed, tiles in a neutral format, palettes as
   colours, text as UTF-8, scripts readable (MATCHING.md's codecs and the
   text and event-script decodings are the start). Each port stops calling
   the original's decoder for that resource, a reviewed change per resource,
   in step with its subsystem retiring the gate, and its accessors name the
   resource rather than the address it was kept at. Widescreen, translations
   and replaceable assets come from here; the readable layout as a whole is
   Phase 5's MD3.

The first belongs to this phase; the second goes resource by resource with
the subsystems.

## Phase 4b: the platform milestones

Serving widescreen and the GPU backends rather than "no emulation", so they
wait behind 4a.

- **P4 widescreen**, and with it the answer to whether `VidFrame` holds up:
  PLATFORM.md's open questions name the assumptions it would expose.
- **Host backends beyond SDL** (TODO.md, "Platform"): rendering on the GPU,
  controllers.

## Phase 5: the long tail

- **MD3**, and the decoded half of the assets it enables (Phase 4a,
  "Assets", the decoded step): a layout meant to be read rather than reassembled, and
  the scripts as text. **MD3 is the prerequisite for
  target 3**, the SNES ROM compiled from the reimplementation -- every pointer
  symbolic, so a routine that differs in length can move -- so it comes forward
  with that target rather than waiting here.
- **The SNES ROM itself** (TODO.md, "A SNES ROM compiled from the hand ports"):
  MD3, then a hybrid that links one compiled routine in place of the original's
  and checks the ROM plays, then outward. `tests/test_port_subset.py` already
  keeps `game/` inside what a 65816 compiler handles, and Phase 3a's decoupling
  is what makes a routine linkable at all: the logic apart from the emulated
  machine is exactly what a SNES build compiles.
- **MD2's remainder**: 42590 census-unknown bytes, 228.3 KiB still `mixed`, and
  `lz` re-encoding, plus the **MD4 residue**. The unknown bytes are by
  definition ones no reader has been found for, so no port depends on them;
  they are proof-of-completeness work, and the formats that ports do depend on
  were typed before the bulk conversion.
- **The asar cross-check**, which has never run.
- **Coverage**: the unrecorded endings, the Reunion route, and re-running
  `--misses` over the whole library.

## Slot in whenever

- **The `fair` and `fair-companion` regression.** After the timing fix these two
  got *worse* against Snes9x and nobody knows why (TODO.md, "Movies and
  coverage"). Investigate when it blocks validation; its age does not establish the cause.
- **CI's macOS job**, which costs 10x minutes on a private repo.

## What this plan deprioritizes, and why

The targets decide this, so the old list is gone: a GBA port and a SNES ROM
compiled from the reimplementation are targets 2 and 3, not interests, and they
are scheduled above (Phase 4a's asset seam, Phase 5's MD3) rather than deferred.
What stays deprioritized is what serves the scaffolding or nothing:

- **M4** (generator speed). The generated C is a means; making it faster helps
  no target.
- **Emulator accuracy**: the IRQ trigger delay, the S-DSP's KON poll,
  re-measuring the audio comparison table, big-endian hosts, the ZSNES state
  import. Out of scope unless one of them blocks validation.
- **The wasm stack question.** The per-port fibers exist only to suspend an
  emulated machine mid-instruction while a port runs; with the machine gone
  there are no call-outs and no per-port fibers. The main thread and menu tasks stay (above), so a third `fiber.h` backend is worth building
  for those when a browser build is wanted, not before. A browser build,
  if it is wanted, is a frontend on top of a finished `game/` -- which is the
  same thing targets 1 and 2 need.
- **Anything that makes the hybrid modes nicer to live in.** `--generated on` and
  shadow mode are how the work is checked, not what is being built.

The host seam was never deferred, because it was the same work as making the
desktop build shippable: the player advances a frame per call and persists
bytes under names (PLAYING.md, "How the player is built"), so a new frontend
needs a driver of its own rather than a fork of the player. That is what makes
targets 1 and 2 a matter of writing drivers once `game/` stands alone.

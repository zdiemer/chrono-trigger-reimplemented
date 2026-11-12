# Plan

The product is ct_native. The targets are desktop native play, experimental
GBA/Wii/GameCube ports, and eventually a SNES ROM compiled from the hand-written
C. Observable divergence is accepted where the game stays structurally the same.

## Working order

1. Make native routes reach their outcomes. Complete frame-end snapshots come
   first: restore the game, coroutine stacks, services, sound and timing debt
   without replaying from power-on. Then finish the route audit, resolve the
   native divergences in TODO.md, and add a new-game-to-ending route.
2. Move the game's state into module-owned C structs (STATE.md), preserving
   cartridge saves, native route queries and the recorded save/place ratchets.
   Resolve aliases, shared scratch and page instances before moving each group.
3. Remove the machine remnants from game/: entry wrappers, registry, held-wait
   rows, emulated stack operations, register escapes and `Snes *` parameters.
   Keep game contracts, timing charges and saveable task state. Use
   `tools/machine_joints.py` to measure the source and the lean native gate to
   validate each change. State conversion and machine removal can proceed in
   separate modules, with the same gate.
4. Finish the native player: persistence, bindings, recording and release
   packaging. Recheck audio stalls and performance on the native program.
5. Decode assets one resource at a time into named, editable data. Then widen
   the renderer and add GPU/controller backends. Keep the portability subset
   for smaller targets; measure stacks and memory before the GBA experiment.
6. Build a relocatable asset layout and a SNES target from the C logic. This
   needs symbolic references and target glue, not an emulation backend.

## The gate

The sampled movies must finish in ct_native with no crash or unexplained dark
ending and preserve their native save/place protections. New Game+ and inventory
item use are the fast routes. Source style/smell and all ROM-free Python/native
unit tests are included in the ten-minute wall budget at `-j 2`. Longer routes
run manually or in a separate batch. See CLAUDE.md for the command.

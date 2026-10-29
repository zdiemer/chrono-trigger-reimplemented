# The hand-run ending routes, as recorded

The per-route accounts of the nine New Game+ ending routes (`tools/movies/routes/`,
run by hand: `HAND_RUN_ROUTES` in `tools/suite.py`), moved from docs/MOVIES.md,
which keeps the mechanism. Each says what the route plays, how it knows the
ending finished, what its shadow run covered over the library of the time, and
how `ct_native`, strict mode and the shipping-mode route check went. Numbers are
as measured when the route landed.

`successor_of_guardia` (technoplaza ct-state17 slot 2, 129223 frames) plays
the telepod, all three Lavos fights with Crono and Marle, the return to the fair,
the royal guard's invitation, the royal family's reveal, and Frog and Leene's
credits through The End. The save's story counter `$27` selects `$6E = 5` in
location `$052`'s ending dispatcher; the route verifies that flag and the final
halt of `$057`'s script, rather than treating a timeout as completion. Short
waypoints pass east of the fountain and the visitor on the stairs; the guard
is approached from the north because his south side is beyond the exit.
Shadow has zero mismatches and refusals, no page/accessor/asset diagnostics,
and identical sound lockstep. Coverage adds 159 instruction bytes over the
48-movie library, none in the `$7E` scene program: this ending starts none of
modes 5, 6, 10/11/14/15 or 12. `ct_native` and strict mode both complete the
129223-frame input with exit 0 and no stop; that alone does not prove the
ending in modes whose timing differs. `route_check --mode on:on:c` reaches
The End at frame 176603, without a wipe; all six restores away from a base
replay exactly, with no C sound-driver errors. The static checks and route-check
unit tests pass. Since no movie joins the library, this route does not require
a narrowed movie-suite run or changes to its aggregate baselines.
The per-ending evidence and remaining saves are in
[movies-wanted.md, target 7](movies-wanted.md#7-the-other-endings).

`legendary_hero` (technoplaza ct-state19 slot 2, 144640 frames) shares the
bucket approach and fights in `other_endings.py`: the shell's boss imitations,
inner Lavos and Core, then Robo and Atropos, Tata's journey, the party in place
of the villains, the staff credits and The End. Story `$5D` selects `$1AB = 1`;
the ending advances it to 6 and finishes in `$025`'s final wait/jump loop,
which the route checks. All fights win without a wipe. Shadow exits 0 with
zero mismatches and refusals, no page/accessor/asset diagnostics, and identical
sound lockstep. It adds one instruction byte over the library (`$C0:A88C`),
no `$7E` code, and starts none of modes 5, 6, 10/11/14/15 or 12. `ct_native`
stops at frame 14237 in `$FD:B438`, an enemy coming into battle (TODO.md);
strict mode runs all frames with exit 0. The shipping route (`on:on:c`) reaches
The End at frame 168451 without a wipe; all 12 restores replay exactly and the
C sound driver reports no errors.

`unknown_past` (technoplaza ct-state20 slot 2, 160225 frames) shares the bucket
and Lavos fights, then plays the credits and character vignettes through Robo's
dance and The End. Story `$69` selects `$1AA = 1`; completion checks flag 6,
object 0 waiting for Robo and Robo's final wait loop. Inner Lavos needed 24
retries, with only winning input retained. Shadow exits 0 with zero mismatches
or refusals, no page/accessor/asset diagnostics and identical sound lockstep.
Coverage adds 92 instruction bytes over the library, no `$7E` scene code;
none of modes 5, 6, 10/11/14/15 or 12 starts. `ct_native` and strict mode both
run the entire input with exit 0 and no stop. Shipping-mode `route_check`
(`on:on:c`) reaches The End at frame 146361; all 12 restores replay exactly,
with no C sound-driver errors.

`people_of_the_times` (technoplaza ct-state21 slot 2, 160570 frames) plays the
bucket, all three Lavos fights and the character credits through The End.
Story `$7E` selects `$05B`; the route follows the credits to `$05F` and checks
object 1's final loop at `$035C`. The default Core seed is 2 (the other fights
use 1). Seed 1 won the battle but the reference sound driver stalled before
the final reveal, waiting for a command echo at `$C7:0635`; another 9000 frames
and button probes did not release it. Seed 2, retried from the Core checkpoint,
finishes. The helper accepts a Core seed offset so that input can be regenerated.
Shadow exits 0 with zero mismatches and refusals, no page/accessor/asset diagnostics
and identical sound lockstep. Coverage adds 16 instruction bytes over the library,
none in `$7E`; none of modes 5, 6, 10/11/14/15 or 12 starts. `ct_native` and strict
mode run the whole input with exit 0 and no stop. Shipping-mode `route_check`
(`on:on:c`) reaches The End at frame 146552; all 12 restores replay exactly,
with no C sound-driver errors.

`the_oath` (technoplaza ct-state22 slot 2, 159243 frames) plays the bucket and
Lavos, Frog's farewell and journey through Magus's castle, the duel, interleaved
credits and The End over the castle. Story `$87` selects `$159` and `$DF` bit 0.
The route checks `$E0 = 8`, `$DF` bit 1 and object `$0D`'s final halt in `$0A5`,
then keeps 600 frames so The End's slow palette fade finishes. The Core needed
18 retries; the movie retains only winning input. It exposed incorrect Frog
healing entries in `policies.py`: the ROM's names and MP costs identify Heal as
entry 3 (2 MP), Cure 2 as entry 6 (5 MP). The corrected table and shared
`party_lavos_core` policy (physical attacks on the left bit before area techs,
then the body and exposed Core) keep this party from wasting its healing.
The Core policy uses Robo's learned Shock (entry 7, 17 MP) and restores MP for
the strongest known area techs. Shipping-mode `route_check` (`on:on:c`) reaches
The End at 161581, the retained fights all without a wipe. The final check
resumed the Core checkpoint after changing only that fight's policy; the
restore replayed exactly, with no C sound-driver errors. The earlier full run
had played the same shell and inner fights with the corrected healing table.
Shadow exits 0 with zero mismatches and refusals, no page/accessor/asset
diagnostics, and identical sound lockstep (499496 ticks, 25 loads). Coverage
adds 181 instruction bytes over the 48-movie library, in field and battle code,
none in `$7E`; none of modes 5, 6, 10/11/14/15 or 12 starts. `ct_native` and
strict mode complete all 159243 input frames with exit 0 and no stop.

`dino_age` (technoplaza ct-state23 slot 2, 176261 frames) plays the bucket,
Lavos, the Reptite version of Crono's home and the other character vignettes,
interleaved credits, Ayla's fall and The End. Story `$8E` takes `$052`'s
`$8A <= story < $99` branch, setting `$7F:015C = 1` and entering `$10F`.
The route requires its seventh pass, object 7's final song loop at `$03FD`,
and 120 more frames; the final picture was inspected. The shell, inner Lavos
and Core finish at 103936, 133140 and 160761, without a wipe.
This save exposed a bot recovery gap: an Attack chosen while the command
cursor was still restoring could open the remembered item list. After that
failed action the bot waited forever for a command menu. `Bot.do` now presses
B to leave that unexpected list, as its Tech/Item failure paths already do.
Replaying the shell checkpoint passes the same failure and finishes the fight.
Coverage adds 31 instruction bytes over the 48-movie library (`$C0:72B2`-`$72B3`
and battle paths in `$C1`/`$CD`), no new `$7E` scene code; none of modes 5, 6,
10/11/14/15 or 12 starts. Shadow exits 0 with zero mismatches and refusals,
no page/accessor/asset diagnostics, and identical sound lockstep (553865 ticks,
24 loads). Core seed 2 is the default: seed 1 finishes at 193389 but has one
`battle_irq` (`$D1:FE97`) shadow mismatch, PPU `$210F` original `FF`, native
`00`, at the wait and 15 later passes. That issue remains in TODO.md; it also
reproduces from the Core checkpoint with generated comparisons off. `ct_native`
stops at frame 15100 in `$FD:B438`, an enemy coming into the battle; strict mode
completes all 176261 frames with exit 0. TODO.md tracks the native stop too.
Shipping-mode `route_check --mode on:on:c` reaches The End at 142332, all three
fights without a wipe, all 12 restores exact, and no C sound-driver errors.

`what_the_prophet_seeks` (technoplaza ct-state24 slot 2, 131287 frames) plays
the bucket, Lavos, the Prophet ending's vignettes, interleaved credits and The End.
The shell, inner Lavos and Core finish at 69092, 95529 and 115627 without a wipe.
Story `$9F` selects `$052`'s `$99 <= story < $D4` branch, outside Memory Lane's
`$A2 <= story < $AB` range: `$F7` bit 7 and location `$190`. The route observes
that selection, then requires the last `$190` pass to clear `$F8` bits 6/7 and
halt object 0 at `$01B6`. Another 120 frames retain the finished picture,
which was inspected.
Shadow exits 0 with zero mismatches, no page/accessor/asset diagnostics, and
identical sound lockstep (411801 ticks, 24 loads). It exposes two guard gaps,
three refusals each: `event_cmd_walk_with_the_party` (`$C0:3002`) and
`field_second_member_follows` (`$C0:A2CE`) while `$190` has an empty second-party
link. They are `gap` entries in `refusal_allowlist.txt` and TODO.md's Refusals
in real play. Coverage adds 377 instruction bytes over the 48-movie library,
none in `$7E`; none of modes 5, 6, 10/11/14/15 or 12 starts.
This ending also proves `$D8:D000`'s entry 77 is a real 18-byte dialog at
`$D8:DD14`, rather than a sentinel. Its resource count is now 78 and the
catalogue includes it, fixing the original asset diagnostic. All previous
resource ranges remain unchanged; the rebuilt catalogue's 6092 resources and
the 42 asset/movie-tool/route-check tests pass.
`ct_native` and strict mode complete all 131287 input frames with exit 0 and no
stop. Shipping-mode `route_check --mode on:on:c` reaches The End at 140017,
without a wipe; all 12 restores replay exactly, with no C sound-driver errors.

`memory_lane` (technoplaza ct-state25 slot 2, 167063 frames) plays the bucket,
Lavos, Marle and Lucca's review of the men, Crono's interruption and The End.
The shell, inner Lavos and Core finish at 94053, 122829 and 157883 without a
wipe. Story `$A2` selects `$052`'s `$A2 <= story < $AB` branch to `$0EF`.
The route advances the dialogue and requires object `$0E`'s final loop at
`$059E`, after the call that reveals The End, plus 120 frames. The final
picture was inspected.
Shadow exits 0 with zero mismatches, no page/accessor/asset diagnostics and
identical sound lockstep (525128 ticks, 24 loads). The seven refusals of
`field_walk_the_leader` (`$C0:881E`) are a documented `gap`: the final upward
exit carries the leader through Y `$FFxx`, outside the guard's accepted range.
Coverage adds 111 instruction bytes over the 48-movie library, none in `$7E`;
none of modes 5, 6, 10/11/14/15 or 12 starts. The missing final dialog resource,
`$DE:C000` entry `$BA` at `$DE:E29D`-`$E2B0`, is now catalogued by correcting
the table count to 187. All old resource ranges remain unchanged; the catalogue
check (6093 resources) and 42 asset/movie-tool/route-check tests pass.
`ct_native` and strict mode complete all 167063 input frames with exit 0 and no stop.
Shipping-mode `route_check --mode on:on:c` reaches The End at 127246 without a
wipe; all 12 restores replay exactly, with no C sound-driver errors.

`reunion` (technoplaza ct-state26 slot 2, 123341 frames) plays the telepod and
Lavos with Marle, Lucca and Robo, the End of Time conversations, Moonlight
Parade, telepod farewells, King, Reunion's special scenes, credits and The End.
The shell, inner Lavos and Core finish at 16619, 54295 and 86129 without a wipe.
Story `$D4`, Crono's absence and clear `$6D` bit 7 select `$6E = 4` and `$054`
in the `$052` dispatcher. The shared story check waits for map loading to
restore bank `$7F`; Reunion also checks `$6E = 4` at the End of Time.
After Gaspar wakes the companions, talking to Robo, Frog and Ayla prepares
their departure; the absent Magus counts himself in at setup. Crossing column
`$13` returns to the fair. The King is approached from the north after the
telepod farewells. RAM proves scene mode 11 at frame 108801 and mode 15 at
115441 (`$7E:0384 = $2B/$2F`). The final maps are
`$051/$02D/$105/$08E/$05A/$056/$058`; completion requires both modes and
`$058` object 0's final halt at `$0052`, then another 120 frames. The End
picture was inspected. None of modes 5, 6, 10, 14 or 12 starts.
Coverage adds 126 instruction bytes (64 starts) over the 48-movie library,
including nine `$7E` scene bytes at `$8CE9/$8CEC/$8CEF`: mode 15's continuation
without another staff roll. The other eight new ending routes add no scene
code. Shadow exits 0 with zero mismatches, no page/accessor/asset diagnostics
and identical sound lockstep (387382 ticks, six loads). It exposes eight guard
refusals at three entries, documented in TODO.md's Refusals in real play.
`ct_native` and strict mode complete all 123341 input frames with exit 0 and no stop.
The two movement refusals are the departure's Y `$FFxx`; three visibility
and three palette-release refusals check an odd sharing link even though
the script owns the palettes. All are `gap` reasons in the refusal allowlist.
Shipping-mode `route_check --mode on:on:c` reaches The End at 125438, all
three fights without a wipe (16744, 49078 and 88617). It observes modes 11
and 15, all 459 restores replay exactly, and the C sound driver reports no errors.

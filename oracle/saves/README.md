# Cartridge saves

Battery saves (8192-byte `.srm`, the cartridge's SRAM) that the movies in
`oracle/movies/saves/` start from. They hold game progress (party, items,
flags, location), not game assets. `ct_dev` and `ct_play` find the one a movie
needs by the CRC-32 in its `# sram crc32` line, so `--sram` is optional; see
[docs/ORACLE.md](../../docs/ORACLE.md).

Each movie's `# Save:` line names the file below and the slot it loads.

## routes/

`cronos-trial.srm`: slot 1 is on the 1000 A.D. overworld outside Guardia
Castle, with Marle in the party and the arrest still ahead (story `$27`, CRC32
`5B31C425`). The `cronos_trial` hand route plays from fantasyanime ctsave01
slot 2 through Yakra, Leene's restoration and the return gate, then saves
through the game's ordinary overworld Save menu. This is the resulting
8192-byte cartridge save, not a machine state; slots 2 and 3 retain the
original archive's games. The short `trial` route loads slot 1.

## fantasyanime/

From the Chrono Trigger saves at fantasyanime.com/squaresoft/ctsaves-srm.htm,
renamed from each archive's `chrono_trigger.srm` to the archive name. Three
slots each, stepping through the story:

| File | Slots |
|---|---|
| `ctsave01-kidnapping` | Truce Canyon in 600 A.D.; the Cathedral; the prison after the trial |
| `ctsave02-future` | the Dragon Tank; outside Bangor Dome; Arris Dome |
| `ctsave03-future_more` | Lab 32; the factory with Robo; the End of Time |
| `ctsave04-masamune` | Zenan Bridge; Denadoro Mountains; the End of Time |
| `ctsave05-prehistoric` | the Forest Maze; the Reptite Lair; the End of Time |
| `ctsave06-magus_castle` | the Magic Cave; Magus's castle entrance; inside the castle |
| `ctsave07-azala` | the Magus fight; Dactyl's Nest; Tyrano Lair |
| `ctsave08-zeal` | Azala and the Black Tyrano; 12000 B.C.; Sewer Access |
| `ctsave09-epoch` | Keeper's Dome; Terra Cave; Mt. Woe |
| `ctsave10-ocean_palace` | the Ocean Palace entrance, middle and the Golem Twins |
| `ctsave11-crono_gone` | the Blackbird; the Earthbound Village; Death Peak |
| `ctsave12-side_quests1` | Death Peak; the End of Time for the side quests; Ozzie's Fort done |
| `ctsave13-side_quests2` | Fiona's shrine done; the Rainbow Shell quest, midway and done |
| `ctsave14-side_quests3` | Sun Stone done; Cyrus done; Geno Dome done |
| `ctsave15-black_omen` | three points in the Black Omen |
| `ctsave16-lavos` | Lavos |

## technoplaza/

The battery saves in the emuWorks state archives at
games.technoplaza.net/snes/chronotrigger/savestates.php (`ct-stateNN.zip`, file
`Chrono Trigger (U).srm`; the archives' ZSNES states aren't here). `ct-state1` to
`ct-state14` follow the story from Queen Leene to the side quests;
`ct-state15` is the final battle (slot 1) and `ct-state16` to `ct-state26` are
New Game+ runs saved (slot 2) at the story point for each of the other endings.

## zophar/

Chrono Trigger saves from zophar.net, renamed to their archive names:

| File | Description from the site |
|---|---|
| `chrono` | Black Omen destroyed, all quests completed, everything found |
| `chrono-1` | Final battle after defeating Lavos' head |
| `chrono-2` | Final level (try getting different endings) |
| `chrono-3` | New Game+ available, a save inside the Black Omen |
| `chrono-4` | Right after beating the Black Tyrano |
| `chrono-5` | Every character but Magus with all levels maxed |
| `chrono-6` | New Game+ with maxed stats; "The New King"; "The Fated Hour" |
| `chrono-7` | New Game+ at the first save point, everyone maxed (for the endings) |
| `chrono-trigger` | Before the final fight, level 50+, all side quests and techs; New Game+ |

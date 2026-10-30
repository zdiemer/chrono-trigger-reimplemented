# Cartridge saves

Battery saves (8192-byte `.srm`, the cartridge's SRAM) that the movies in
`oracle/movies/saves/` start from. They hold game progress (party, items,
flags, location), not game assets. `ct_dev` and `ct_play` find the one a movie
needs by the CRC-32 in its `# sram crc32` line, so `--sram` is optional; see
[docs/ORACLE.md](../../docs/ORACLE.md).

Each movie's `# Save:` line names the file below and the slot it loads.

## routes/

`rainbow-shell.srm` is a normal cartridge save made by playing fantasyanime
ctsave13 slot 1 through Giant's Claw in 600 A.D., using a Shelter at its save
point and saving over slot 1. The file contains cartridge SRAM, with no memory edits.
Crono, Lucca and Robo stand at location `$06D`, tile (61, 21), before Rust
Tyrano (`$7F:01D2` bit 6 clear). The hand-run `rainbow_shell` route reproduces
the approach and writes `<prefix>.srm`; `rust_tyrano` records the short boss
movie from this save. The recorded approach took 55184 frames and is kept
outside the movie library. Slots 2 and 3 retain the source archive's games.

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
| `ctsave13-side_quests2` | Fiona's shrine done; before Giant's Claw, after shell delivery but before the trial, and after the trial |
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

## routes/

Cartridge saves made by playing the route scripts, then saving through the
game's menu. These contain SRAM only, not machine copies.

| File | Slot | Progress | CRC-32 |
|---|---|---|---|
| `luccas-mother-before-campfire` | 1 | Outside Fiona's shrine in 1000 A.D.; Robo restoring the forest, before the campfire and Lara's rescue | `42DD86E1` |

`luccas-mother-before-campfire.srm` was played from technoplaza `ct-state11`
slot 1: defeat Retinite, leave Robo with Fiona in 600 A.D., fly the Epoch to
1000 A.D., land and save. `tools/movies/routes/prepare_luccas_mother.py`
repeats those prerequisites and exports the game's SRAM; its long movie is a
hand-run route kept outside the library. The library's `luccas_mother` route
starts from this boundary. The preparation's 40031-frame replay reproduces
this SRAM byte for byte.

The stock saves bracket too much: fantasyanime `ctsave12` slot 3 still has
the desert closed (`$7F:00F7 = $00`), while `ctsave13` slot 1 has Lara already
saved (`$7F:007C = $96`). `ct-state11` slot 1 has the seed planted
(`$7F:00F7 = $0A`) but Retinite unbeaten (`$7F:01A3` bit 0 clear).
The prepared save has `$7F:019E = $F9` (Robo working, not recovered) and
`$7F:007C = $0E` (campfire and rescue still unplayed).

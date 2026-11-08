# The telepod route's battle item counts

At `67bdb21c`, the full `telepod_lavos` route reached the Dream Project ending
in the default build but exhausted its 400,000-frame fight limit in native
shape. A fresh reproduction gave the same failure in 338 seconds; the default
run won the Core at frame 122926 and reached the ending at frame 144756.

Frame-by-frame reads with identical input show timing differences before the
field: a palette animation differs at frame 130, and the load menu's frame
counter first differs at frame 1722. At frame 2422, the menu saves its counter
to `frames_at_leaving` (`$7E:29AD`): `$BC` in the default build, `$BD` in native
shape. The battle uses that byte as its random-table position. Both routes
reach the shell at frame 5102, but their fights take different paths.

The failed Core fight starts with eleven MegaElixirs and accepts eleven
MegaElixir commands, the last at frame 112335. At frame 120958 the bot selects
another: its inventory read still reports eleven, while battle item row 8
correctly holds zero. The menu refuses it. The live list still has 83 Full
Tonics, eleven Elixirs and seven Revives, but repeated attempts at the empty
row prevent the bot from healing. The default run also illustrates the stale
read: its winning checkpoint has eight MegaElixirs in the bag and zero in the
battle list.

The item port already has the required behavior: `chose_item` reserves a unit
in the battle list when a command is queued, and `battle_put_items_back`
updates the bag as the fight ends. The route's `Bot.inventory` now reads that
live list, and `policies.item_index` preserves its row numbers, including
empty and exhausted rows. No port, random seed, fight limit, baseline or
exception changes. Regression tests cover depleted stacks, empty rows and
reservations before the bag is updated.

With the corrected reads, native shape's first 124 battle decisions have
identical logs: all the shell and inner fight, then Core decisions 0 through
69. It skips the exhausted MegaElixir for Luminaire at decision 70. It wins the
Core at frame 148888 with no retries: 96691 frames after entering the fight,
well within the unchanged 400000-frame limit.

Both full routes reach the Dream Project ending from scratch, with the same
seed and limits as before:

| Build | Core won | Ending reached | Wall time | Exact checkpoint replays |
|---|---:|---:|---:|---:|
| Default | 122926 | 144756 | 1042 s | 46 |
| Native shape | 148888 | 170918 | 988 s | 38 |

Neither run has a differing replay or loses suspended work. The corrected
default run's 126 battle decisions match its original run, and its winning
Core checkpoint is byte-identical. The Python suite passes 561 tests, with
ten skipped.

The sampled gate with one worker and `--evidence`, without long routes or
all-movie coverage, finishes with `suite OK`: 37 shadow movies clean, and
37 native-shape movies plus both fast routes reaching their outcomes with
zero failures excepted. No baseline or exception files changed.

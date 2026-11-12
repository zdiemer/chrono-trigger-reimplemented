# State storage: the second slice

## Ownership and measurement

The second slice moves 313 variables: 245 additional owners and 68 alias views.
Together with the four location records, there are **249 owner variables and
68 aliases**, with nine internal field aliases and **4,072 serialized bytes**.
The owner-module grouping is finer than the map's subsystem grouping: live
sound has four variables; palette owners and their views have nine, and the
sprite buffer and its views have five. These counts include compatibility
cells that no longer carry live computation; they do not measure live logic.

| Map subsystem | Image-backed before | Image-backed after | Total variables |
|---|---:|---:|---:|
| core | 85 | 51 | 89 |
| data | 8 | 0 | 8 |
| event | 20 | 11 | 20 |
| field | 308 | 178 | 308 |
| menu | 467 | 415 | 467 |
| menu_object | 39 | 39 | 39 |
| overworld | 42 | 29 | 42 |
| battle | 922 | 904 | 922 |
| scene | 346 | 307 | 346 |
| warp | 22 | 12 | 22 |
| **Total** | **2,259 (99.82%)** | **1,946 (85.99%)** | **2,263** |

The map's explicit metadata is the per-variable ledger. The selected slices
include field/dialog control, menu controls and glyph state, overworld vehicle
state, battle setup and remembered state, scene controls, and warp's palette
reuse. Field screen/movement cells share the battle's X/Y owners, and scene
fade cells share the field's window-corner owner. Shared initial contents are
preserved immediately, without a mode-transition copy.

Data's eight cells are decompression inputs, its output length, and retired
decoder scratch. Its output buffers belong to the callers, not to those cells:

| Output family | Classification and storage boundary |
|---|---|
| Field map tables, scripts and object graphics | Decoded resources and mutable map/script buffers, still in the large field/event overlap groups |
| Battle animation and screen buffers, menu/overworld staging | Decoded graphics, still in shared resource scratch |
| Scene programs, text, pictures and staging | Decoded programs/resources; shared buffers remain image-backed |
| Palettes and sprite output | Mutable rendering data; selected owners are reached through the same output view |

`data/decompress.c` writes literals through `hw_port_write` and backreferences
through `wram_read8/write8`; both already reach the address view. Moving its
parameter cells does not claim that all decoded buffers have moved. The live
sound slice is `current_song`, `sound_command`, `sound_command_parameters` and
`sound_driver_effects_asked`; obsolete upload-driver state stays separate.

The raw-client audit retains format/dynamic clients as compatibility views:
`menu/saves.c`'s projection, new-game initialization and bulk clears; palette
streaming writes; video's `read_game` callback (including low-bank sprite
sources); `rom_host.c`'s RAM-selected resource pointers; and native route and
end-state reads. Each reaches `state_host_read8/write8`, with no direct read of
a migrated cell in the residual image. Typed game callers need no edits.
This resolves their storage coherence, not their dependence on addresses.

Of the remaining variables, **1,921 overlap others** and 25 are isolated
retired sound-driver cells. The small vector-stub and old sound-pointer/list
alias groups are retained for the machine/driver removal rather than given
new compatibility owners. The five largest overlap components contain 1,839
variables: battle/menu graphics scratch (965), object/menu pages (381), shared
low scratch (299), scene/field buffers (99), and bank-$7F resources (95).
None has an existing variable containing every declared byte. They need
semantic owners or views composed from several owners, including page
instances, rather than an anonymous array replacing each address span.

The residual 128 KiB allocation remains; migrated addresses in it are unused,
not mirrors. This measures ownership moved, not memory saved. Removing the
allocation also requires the remaining unnamed address clients to move.

## Validation

The warning-as-error build passed. `ram_map.py header --check` and
`ram_map.py accessors --check`, `vars_host_test`, `native_time_test`,
`unpack_time_test` and the emulator-backed `vars_test` passed. Both
`portstyle.py --check` and `portsmell.py --check` passed. No baseline or
exception was added. The Python unit suite passed all 626 tests without skips.
The sampled gate's 58 shadow movies were clean; the 7,200-frame generated-only
trace matched the reference, and the timing, video-write, fallback and refusal
checks passed. Both fast routes reached their outcomes in the shipping build
and in `ct_native`, including Lavos's shell and Beyond Time's second form.
All 58 movies and both routes passed with scaffolding compiled out. All 58
movies reached their end in strict mode and in `ct_native` with visible output;
both recorded-outcome ratchets passed. These are baseline checks, not a claim
that every final state or picture equals the reference (28 strict and 23 native
end states matched). Sound fuzzing retained both expected digests.
The sampled `tools/suite.py --ct-dev build/main/ct_dev.exe -j 2 --evidence`
gate ended `suite OK`; neither the whole-library nor long-route option was used.
Gate logs and derived artifacts remain under the ignored
build directory; the evidence cache is local to the worktree.

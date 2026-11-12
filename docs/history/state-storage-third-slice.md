# Native ownership before the typed-storage review

These historical ownership counts included blob-backed views. They are not
typed-storage counts; see [STATE.md](../STATE.md) for the corrected measure.

## Migrated modules and remaining clients

The third slice moves 1,919 more variables, leaving **27/2,263 (1.19%)**
image-backed. The 2,236 migrated variables comprise 278 owner variables and
1,958 views. Another 48 storage-only records supply semantic owners without
adding game accessors or changing the variable-count denominator. Together
these owners hold **129,365 serialized bytes**, with 207 internal field views.
The counts include compatibility state; they do not measure live computation.

| Map subsystem | Image-backed before | Image-backed after | Total variables |
|---|---:|---:|---:|
| battle | 904 | 0 | 922 |
| menu | 415 | 0 | 467 |
| scene | 307 | 0 | 346 |
| field | 178 | 0 | 308 |
| menu_object | 39 | 0 | 39 |
| core | 51 | 27 | 89 |
| overworld | 29 | 0 | 42 |
| event | 11 | 0 | 20 |
| warp | 12 | 0 | 22 |
| data | 0 | 0 | 8 |
| **Total** | **1,946 (85.99%)** | **27 (1.19%)** | **2,263** |

Ownership follows game records and resources, including their reuse by other
modes. Combatants have typed HP, MP, status, experience and equipment fields;
script-selected stat offsets serialize those same fields. Nine menu records
hold sprite/animation state, and the scene arithmetic record has its own and
two channel instances. Menu task records own their task fields. The scratch
views retain shared initial contents immediately, without transition copies.

Large resources crossing those records are composed views, with separately
owned portions where needed. For example, field map rows before and after the
combatants own their layer bytes; two boundary portions retain the map bytes
on either side of the combatant array. The rows through the array view the
combatants themselves. The physical stride remains projection metadata, while
the C arrays omit the intervening cells owned by other resources. This does
not create a replacement array spanning an entire overlap component.

The same rule covers battle item rows around menu direct pages, object records
around scene channels, shared screen/palette scratch and interleaved field
attributes. Resource buffers remain buffers: event scripts, decoded graphics,
map rows and staging bytes do not become invented scalar fields. Unchecked
inventory, combatant and table tails still reach the next owner through the
address projection, with the original index arithmetic and diagnostics.

The raw-client audit retains format/dynamic clients as address views:
`menu/saves.c`'s projection, new-game initialization and bulk clears;
decompression and palette streaming; video's `read_game` callback;
`rom_host.c`'s RAM-selected resource pointers; and native route and end-state
reads. All already reach `state_host_read8/write8`; the private residual image
has no game client bypassing the projection. No game algorithm needed a new
raw-address escape or an accessor conversion to make this migration coherent.
Their remaining address dependence is separate from their storage ownership.

The remaining 27 variables are retired sound-driver state, covering 257
bytes. They include the old pointer/list group as well as isolated driver
cells. Resolve their compatibility clients with driver/machine removal rather
than allocating new native owners for the obsolete driver. The residual
128 KiB allocation remains: 1,707 addresses have no owner, including these
257 bytes and 1,450 unnamed bytes/padding. Removing that allocation requires
an audit of dynamic clears, script-selected accesses and unchecked tails into
those holes; zero named variables alone is insufficient.

## Validation

`vars_host_test` checks typed combatants against whole-record script views,
all menu and scene page instances, 24-bit initialization/truncation, a view
crossing two owners and a view crossing two fields, independent instances,
invalid-index fallbacks, full-address projection, video sourcing and the real
save/load/checksum routines with a committed cartridge `.srm`. The Python
map tests reject missing owners, chains, duplicate ownership and incomplete
compositions, and verify storage-only records do not add API variables.

The Windows MinGW Release build passes with `CT_WERROR=ON`. All 629 Python
unit tests pass, as do `vars_host_test`, `vars_test`, `native_time_test` and
`unpack_time_test`.
The generated-file checks and both `portstyle.py --check` and
`portsmell.py --check` pass. An additional generated accessor sweep checks
713,158 reads and writes against the serialized address view with zero
failures, both with accessor diagnostics enabled and disabled.

The sampled gate with `-j 2 --evidence` ends `suite OK`: all 58 shadow movies
are clean, all 58 strict and native movies reach their ends, and both fast
routes reach their outcomes in shipping, native and scaffolding-free builds.
The native route outcomes are Beyond Time's second form at frame 3,770 and
the telepod's Lavos shell at frame 5,162, with no route exceptions. Trace
identity, recorded-outcome ratchets and the remaining gate checks pass;
no baseline or exception was changed. Validation used neither `--all-movies`
nor `--long-routes`.

The prior slices' measurements are in
[history/state-storage-second-slice.md](history/state-storage-second-slice.md)
and [history/state-storage-first-slice.md](history/state-storage-first-slice.md).

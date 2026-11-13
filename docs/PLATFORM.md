# Native platform services

Game code calls video, audio, timing, input and save interfaces in
`runtime/platform/`. Their implementations in `runtime/platform/native/`
are linked into ct_native. The frontend owns host events and files. Services
express tiles, palettes, priority and window logic; game code does not write
hardware registers.

## Video

`video.h` defines the scene state, uploads, sprites and line effects. Calls
update a `VidObserver`; the renderer consumes that state and owns its framebuffer
between frames. VRAM remains a 64 KiB tile store, with palette and sprite memory
and per-line effects. The native renderer implements background modes, priority,
windows and color math. Dirty tracking avoids rebuilding unchanged material.
No emulated PPU is used, and no call names a register: the SNES-only escapes
(a raw copy and a register write) and the older forms that took the SNES's
write order (an upload per transfer, a transfer record) are gone. Two transfer
descriptions the game keeps in its state still hold the VRAM data port's low
address byte as the original stored it (issue #206).

## Audio

The C sound program receives game commands through the audio service. Voices
supply sample playback, envelopes, pitch, noise and echo to the native mixer.
The SDL driver pulls buffered output; game code never polls an audio device.
A snapshot keeps the whole sound driver and mixer (docs/PLAYING.md,
"Snapshots"). See SOUND.md.

No register write reaches the mixer. The driver's command `$FD`, which sets
one setting by its S-DSP register number, is decoded in the driver
(`game/sound/driver.c`, `set_by_hand`) into the voice calls that mean it; a
write to part of a setting reads what the voices are set to
(`au_voice_settings`, `au_mix_settings`) and sets the whole. The mixer's
switches became separate calls (`au_output_muted`, `au_voices_reset`) and the
echo's buffer can be placed by hand (`au_echo_buffer`), as the command can.
`sound_setting_test` checks each setting against the mixer.

## Timing without a beam

The game advances in frames. Its main thread and menu tasks use the coroutine
interface in `runtime/port/fiber.h`; the host resumes work at frame boundaries.
Native work charges accumulate debt, with fractional carry and excluded reads
handled by the state host. Existing rates in `runtime/port/time_rates.c` are
frozen authored estimates. Changing a rate requires evidence from native play.
The timing, native-time, task and unpack-time tests exercise this accounting
without a ROM. Core owns typed mode callbacks in game state; the frame driver
dispatches them in the established order. The program test checks callback
isolation, replacement and mode hand-offs. Removing a state read preserves its
counted work explicitly where pacing depended on it (REIMPLEMENTATION.md).

## Input, saves and resources

Input arrives as controller words each frame. Persistence transfers bytes under
names; the frontend supplies files and handles errors. Cartridge saves retain
their slot/checksum format, tested through the real game routines.

`re/assets.tsv` is the resource catalogue and `re/asset_views.toml` defines typed
views. `tools/assets.py generate` emits the committed lookup table. On first run,
the frontend verifies the user's cartridge and imports a local pack. Game code
loads named resources from that store, never an embedded ROM. Packs and decoded
assets are not committed.

### Palettes, the first decoded resource

A catalogue row whose codec is `palette:N` (or `palette:NxM`) is BGR555
colors, N a row and M rows a palette: the field's location palettes
(`palette:15x7`, 88 of them) and sprite palettes (`palette:12`), the battle
sprites' three- and seven-color sets, the battle screen's effect, gradient and
background colors, and the party's color cycles. Each was split out of the
mixed or untyped region that held it, its extent where the accessor's indexing
and the data agree (every word below `$8000`; the cycle's `$FFFF` tail is its
own padding row). `tools/assets.py check` holds a palette row to whole words.

`frontend/palettes.c` writes every palette resource a store serves as text, a
color a line (`red green blue`, 0-31, and `+` where the word's unused top bit
is set, so any word survives), under a `[name]` line and row comments, and
reads such text back into a store over the pack. `ct_native` keeps the text
beside the pack (`<pack>.palettes`): where there is none it checks that every
palette written and read again gives back its bytes, writes the file, and on
every start remakes the pack in memory from the store over the text, so the
game's existing accessors read the decoded palettes with `game/` unchanged.
The text is from the user's cartridge and, like the pack, never committed.

Measured: `palettes_test` (ROM-free) writes and reads back a made-up store's
six palettes, serves an edited color and only the palettes a text names, and
refuses wrong text at its line; with `roms/ct-usa.sfc` staged it reads back
every cartridge palette as its bytes. The lean gate runs every movie, route
and snapshot on the decoded palettes, since each run imports its pack and
text. Swapping red and blue in the field's palettes shows in the first field
scene's picture.

## Further targets

Widescreen reaches the game as the video service's view, `vid_view`: how much
of the picture is shown, the original's 256x224 unless the driver chose another
with `vid_host_set_view` (a snapshot keeps the driver's choice). The gate runs
at the original's (docs/PLAN.md, "Layering"). A port asks it where it decides
how much to show or where to lay something out, never where a rule of the
game's uses the screen's size: the field's object window at `$C0:A88D` decides
which objects run, so a wider picture needs a draw-only test beside it rather
than a wider window; the sprite-object mode's in-view test
(`menu_scene_object_in_view`) is such a rule too, its scripts branching on it.

### The wider view, as built

`ct_native --view WIDTHx224` (256 to `VID_MAX_VIEW_WIDTH`, 480) chooses the
view; the window and the screenshots take its width. A wider view keeps the
original's screen coordinates and puts that screen in its middle: it reaches
from `vid_view_left` (negative) to short of `vid_view_right`.

- **The renderer** draws `VidFrame.view`'s width. Each layer is sampled from
  its scroll plus the view's left, but only as far across as the frame says
  it holds the picture (`vid_show_a_layer_across`): every frame starts with
  each layer holding the original's screen alone, so a tilemap never repeats
  or shows stale columns in the margins unless its mode built them, and what
  lies under it shows there instead -- in the end the backdrop, the colour the
  original shows where no layer is opaque. A line whose tilemap is one screen
  across (32 tiles: the menus', the battle's and the dialog's panels, which
  line effects switch to partway down) keeps to the original's screen. The
  original's screen itself is always drawn as before. Mode 7 continues its
  plane.
- **Windows** keep byte edges, as the game's line tables hold them. An edge on
  the screen's first or last dot reaches on to the view's side (`VidWindows`):
  a full window covers the view, a half-screen wipe its half, and a shape
  inside the screen stays where the original drew it. The tables themselves are
  the game's memory and are not widened.
- **Sprites** carry a place beside the table (`VidSpritePlace`, sent with it by
  `vid_send_oam`): the whole signed X a builder put a sprite at, or that it hid
  it. A place holds while the entry keeps the X and Y it was given with; a
  sprite without one reads the table's nine bits as the original does, so it
  never shows in the right margin. The field's objects give their whole X
  (they run only near the screen, so nine bits far left of it are right of
  it), the battle's combatants theirs, hiding what leaves the screen up or
  down; the battle's cursors, the scenes' leftover group entries and the time
  warp's spare sprites are hidden. The sprite limits count the original's
  screen. Each mode takes the frame over with `program_hand_the_vblank_to`,
  which forgets the last mode's places, so none outlives its mode to place an
  entry the next writes with the same X and Y.
- **The field** keeps its screen's edges, which decide which objects run
  (`$C0:A88D`), and draws past them: as its edges come in, and for two frames
  after a map's screen is sent, `field_fill_the_views_margins` builds the
  layers' columns either side of the screen from the map into a line of its
  own and sends them in the vblank (`vid_send_view_vram`, below): the map's own columns, which its tables
  hold whatever the screen's limits within it. Past the map's width a column
  is built as the edge's, and shown only where the edge is one entry all the
  way down (a room's void), which is judged as the margins are built and kept
  for the frames between, when a battle's data may lie over the map's looks;
  elsewhere the layer stops at the map. Each frame
  `field_show_the_layers_across` says how far each layer holds the map: a
  streamed layer those columns, a layer drawn whole its one copy of the map
  within the map's width. Objects show in the margins within the
  original's activation window (two map tiles past the screen) and not beyond.
- **The battle** is fought over the field's layers and shows them as far as
  the field last built them, moved with its own scroll
  (`field_show_the_layers_under_a_battle`); a battlefield that brings its own
  BG3 keeps that layer to the original's screen.
- **The world maps** fill their two scrolling planes' margins the same way
  each frame (`menu_map_fill_the_views_margins`) and show them across the view.
- **Every other mode** (the menus, the scenes, the time warp's tunnel aside,
  which is Mode 7) keeps its layers to the original's screen, the backdrop in
  the margins, until it builds what lies past it.

A wider view's own work reads the game's state outside its time
(`charge_uncounted_begin`) and sends in the vblank, where a send takes none;
its state (`SpritePlaces`, `FieldMargins`) is the program's, beside the game's
memory. Its columns go to VRAM through `vid_send_view_vram`, which shows them
but keeps them out of what the game reads back (`vid_vram_word`: the palace
rising copies the field's map out of VRAM) and leaves the VRAM port's step and
address where the game's own sends left them. So a movie played at a wider
view ends in the same work RAM and save RAM as at the original's.
`tools/wide_check.py` plays each gate movie at 256x224 and 480x224 and
compares the whole end state, work RAM and save RAM: all 58 end the same,
`attract-demo` through the palace's rise among them. It plays the gate's
movies twice over, about six minutes at `-j 2` beside the gate's own five, so
it runs in the non-blocking `suite.py --long` batch rather than the gate.

GPU backends must preserve line effects and color math. Smaller targets need measured
memory and coroutine stack budgets; a callback-based frontend needs an appropriate
fiber backend or resumable game tasks.

## Scene map fills

`vid_fill_vram_half` fills the chosen low or high byte of VRAM words, preserving
the other byte and wrapping addresses. Native tests cover byte selection,
wraparound and untouched bytes. The title's BG1 clear, the unused-plane fill
and the credits' map (its tiles, then its in-front bytes) use it. The title
clear removes stale field tiles after game over and attract playback; three
native picture fingerprints protect this change (REIMPLEMENTATION.md).

## Memory moves and VRAM read-backs

Eighteen of the last nineteen register escapes were video transfers and a
setting the original programmed from its own tables (the nineteenth, the
sound driver's register write, is under Audio). Each is now one of four things:

- **Memory the game moves itself.** The scenes' clears copied the Mode 7
  multiplier's product over memory, and the menus filled and copied work RAM
  through its port. The game now writes the memory (`wram_fill8`,
  `wram_port_fill`, `wram_port_copy`) and charges the move with
  `tm_charge_transfer`, which costs the main thread what the SNES's transfer
  of as many bytes did and leaves a handler's moves to the frame, as the
  escape's host half did. The matrix values the original multiplied with are
  still set: they are video state.
- **Sends from where VRAM's address stands.** The scene program's loaders set
  the address, unpack, then send in the mode their call cells keep.
  `vid_continue_vram` and `vid_continue_vram_half` send whole words or one
  byte of each word from the cursor, stepping as the port says; the scene
  decodes its cells' mode and port into one or the other. The race's plane
  halves and column use the half forms, the battle's described transfer
  `vid_send_vram_in_mode`.
- **VRAM read back.** `vid_vram_word` answers a word as the game's uploads
  left it, never a wider view's own (`vid_send_view_vram`).
  The race copies its plane's column and the palace its map out of VRAM with
  it, keeping what the original's port reads give: the first read after the
  address is set hands back the prefetched word, which the next read fetches
  again, so each copy's first entry is doubled and the rest lands one place on.
- **A setting the game's data names by address.** The menus' object scripts
  name the sprites' size and tile-place byte by its SNES address;
  `vid_setting_at` says which setting an address is, and the script's write
  becomes `vid_set_obj_config`.

Natively, the race's plane halves and column, the palace's read-back and the
credits' map were counted and dropped before; they now draw. The palace's
blank rows were filled with the product's low byte, 0; the original copies the
product a word at a time, tile `$100`, which the port now writes. The gate's
`attract-demo` fingerprint moved with these pictures, twenty samples in the scene
program's two passes (tools/native_picture_baseline.txt).
`tests/port/video_test.c` covers the cursor sends and the read-back,
`tests/port/timing_test.c` the transfers' time.

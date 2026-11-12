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
address byte as the original stored it (TODO.md, Platform).

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

## Further targets

Widescreen must review coordinate clipping and field activation bounds. GPU
backends must preserve line effects and color math. Smaller targets need measured
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
- **VRAM read back.** `vid_vram_word` answers a word as the uploads left it.
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

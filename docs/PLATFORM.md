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
`native_escapes.c` implements the remaining address-shaped transfer operations;
callers are listed by the platform lint and should become named operations as
machine-removal work proceeds. No emulated PPU is used.

## Audio

The C sound program receives game commands through the audio service. Voices
supply sample playback, envelopes, pitch, noise and echo to the native mixer.
The SDL driver pulls buffered output; game code never polls an audio device.
Route restores replay audio-host replies exactly through `frontend/audio_replay.c`
so a checkpoint replay reaches the same game state. See SOUND.md.

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
wraparound and untouched bytes. The title's BG1 clear and unused-plane fill
use it instead of a SNES transfer escape. The title clear removes stale field
tiles after game over and attract playback; three native picture fingerprints
protect this change (REIMPLEMENTATION.md). Plane uploads and the credits' map
fill remain follow-ups.

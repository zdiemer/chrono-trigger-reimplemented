# Approach

Chrono Trigger is reimplemented as readable C running in ct_native. Game logic
uses named state operations and platform services; the host supplies video,
audio, input, timing and persistence. User-owned cartridge data is imported
locally and never distributed.

Native movies, recorded saved-game/save-RAM/place outcomes and state-driven
routes protect behavior. Source quality and ROM-free unit tests share the lean
gate. See PLAN.md for the work order, PORTING.md for the source bar, STATE.md
for storage, PLATFORM.md for services and PLAYING.md for build instructions.

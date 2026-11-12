Chrono Trigger reimplemented: native player

This build contains no ROM, graphics, music or other game assets. Supply your
own Chrono Trigger (USA) SNES ROM, No-Intro CRC32 2D206BF7, 4 MiB. A 512-byte
copier header is accepted. Other regions and modified ROMs are rejected.

First launch:
  ct_native "path/to/Chrono Trigger (USA).sfc"
On Windows you can drop the ROM onto ct_native.exe. The player imports an
.assets pack beside the ROM. This directory must be writable; use
  ct_native --rom "path/to/game.sfc" --save-dir "writable-directory"
to put the pack and saves elsewhere. Create that directory first.

Later launches need no arguments and no ROM: the player remembers the pack
in its per-user preferences directory. --assets selects a pack explicitly.
Keep your ROM available for --import ROM if a new build changes the catalogue.
Do not distribute the imported pack.

Windows: keep the supplied DLLs beside ct_native.exe.
Linux: install the SDL2 runtime (Debian/Ubuntu: libsdl2-2.0-0).
macOS: install SDL2 with Homebrew (brew install sdl2). This is a command-line
application, not an app bundle. Use the artifact matching your OS/architecture.
After extracting on Linux/macOS, run chmod +x ct_native before starting it.

Controls: arrows move; X/Z/S/A are SNES A/B/X/Y; Q/W are L/R;
Return is Start; Right Shift is Select. Gamepads use face buttons by position,
D-pad/left stick, shoulders, Start/Back. P pauses, Tab/right trigger fast
forwards, F11 or Alt+Return toggles fullscreen, Escape quits.
ct_native.keys beside the executable supplies custom bindings. --keys FILE and
repeatable --bind "INPUT=ACTION" override them; for example --bind "Space=A".

In-game saves use an 8 KiB .srm beside the pack/ROM (or in --save-dir).
--sram FILE opens another cartridge save; window play writes completed saves
back to it. Movie playback/recording and headless checks never overwrite it.

F5 saves the current frame to .native-state; F7 loads it at once. The state
holds the whole game, its sound and its suspended routines. It loads only in
the same build of ct_native (on macOS, only in the session that saved it) and
is separate from emulator .state files. Keep in-game .srm saves for progress
across build updates.

--record FILE.ctm records from power-on with empty SRAM, or the --sram save.
Recordings use the .ctm input format described in docs/MOVIES.md.
Recordings flush periodically and on normal exit. During recording only a
quick save from that recording loads; loading rewinds the recorded input.

This is a development build. Known game/timing divergences remain; a movie
reaching its last frame does not prove the route completed. Run from a
terminal to retain diagnostics and include them with bug reports.
ct_native --help lists further options.

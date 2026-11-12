# Native sound

The C sound program in `game/sound/` plays through the native voices and mixer.
Addresses below describe the cartridge sound format; no sound CPU is executed.

## The driver in a paragraph

Block 0 (7151 bytes at `$0200`) is 6.5 KiB of code and 0.7 KiB of tables.
After initialising the S-DSP and the timers it sits in one loop (`$0299`) that
polls port 0 for a command and waits for timer 0, which ticks at about 190 Hz
(8 kHz over 42). Each tick it flushes seventeen shadowed global DSP registers
(key on last), reports its status on the ports, advances the fades, runs the
sequencer, turns each voice's state into volume and pitch writes, and executes
the commands the CPU queued during the tick. The sequencer drives 16 tracks: 8
of music (one per DSP voice, sharing a tempo) and 8 of sound effects, of which
the upper four are used and take voices 4-7 over from the music while they
play, at a fixed tempo of their own. A track is a byte stream: a byte below
`$A8` is a note (pitch and one of 14 lengths packed together), `$A8-$B5` a tie,
`$B6-$C3` a rest, and `$C4-$FF` are 60 event commands (volume, pan, slides,
vibrato, tremolo, pan sweep, octave, transpose, instrument, ADSR, loops, jumps,
echo, noise, pitch modulation, tempo, conditional branches on a value the game
sets, a marker the game waits for). Everything a track does ends in S-DSP
register writes; the driver has no mixer of its own.

## Sound RAM

| Range | What |
|---|---|
| `$00-$EF` | Driver variables (direct page). `$00` the loaded song's relocation offset; `$02+X` each track's read pointer and `$26+X` its tick countdown (X = 2 x track, 0-`$1E`); `$22`/`$23` the key on and key off shadows, `$53` the music tracks playing, `$86` the voices the effects own; tempo, echo and filter state from `$46` to `$7C`; `$D0`/`$D1` the voices whose volume or pitch changed this tick |
| `$F0-$FF` | I/O registers |
| `$0100-$01FF` | Stack from the top down; per-track bytes below it (`$0110-$0191`: vibrato and tremolo delays and depths, reached with the direct page moved to `$01xx`) |
| `$0200-$1DEE` | Boot block 0: code to `$1543`, dispatch tables `$1544-$171F`, code `$1720-$1D6C`, tables `$1D6D-$1DEE` (the semitone pitch table, a vibrato depth scale, the echo filter presets, the 14 note lengths, a one-byte silent track, and a built-in looping sample, directory entry `$10`, whose loop the driver itself rewrites a byte a tick, `$0B5C`) |
| `$1E00-$1EFF` | The sample directory (the DSP's DIR is `$1E`); block 3 fills its first 9 entries, song uploads the rest |
| `$1F00-$1F7F` | Per instrument: a tuning word (block 5, then uploads) |
| `$1F80-$1FFF` | Per instrument: its ADSR (block 4, then uploads) |
| `$2000-$2EFF` | The current song: a relocation base, two sets of 8 track pointers (commands `$10`/`$14` start the first, `$11`/`$15` the second), then the tracks |
| `$2F00-$46FF` | One of the two effect sets command `$70` switches between (block 1 is the first): a table of 4-byte entries, two track pointers per effect, then the effects' tracks. Events `$E9`/`$EA` also jump into it |
| `$4700-$4900` | Block 2 (513 bytes: BRR data every song keeps) |
| `$4901-...` | The song's samples, in the slots the CPU half allocates (in bank `$C7`) |
| `...-$F0FF` | The echo buffer, ending at `$F0FF` and growing down by 2 KiB per unit of EDL (`$1352`) |
| `$F120-$F143` | The song's 12 percussion entries (instrument, note, pan), for tracks in percussion mode |
| `$F160-$F171` | Three volume fades (music, effects on voices 6-7, effects on voices 4-5) |
| `$F172-$F1B1` | The command queue, 16 entries of 4 bytes |
| `$F200-$F5FF` | Per-track arrays, 32 bytes each (one word per track): octave, instrument, volume and pan with their slides, pitch and its slide, vibrato, tremolo and pan sweep state, transpose, detune, ADSR |
| `$F560-$F5FF` | Per-track loop stacks (4 levels: counts and return pointers) |
| `$F800-$FF8F` | The saved song, for resuming it: `$F200-$F7FF` and the direct page and page 1 (`$13A9` saves, `$13E5` restores) |

## Boot and the main loop

- `$0200` clears the direct page, mutes the echo volumes, sets DIR, puts every
  voice in a fast GAIN decrease, clears the command queue, sets the timers
  (0: `$2A`, about 190 Hz; 1: `$80`, 62.5 Hz), allocates the echo buffer
  (`$132E`, delay 5), installs the built-in sample's directory entry and
  instrument, sets the master volume and enters the loop.
- `$0299` loop: `$0B96` takes a command waiting on port 0 at once (this is where
  song starts arrive); otherwise it waits for timer 0.
- The tick (`$02A0-$0374`):
  1. Write the 17 shadowed registers from the table at `$16FF`/`$1710`: FIR7-0,
     EFB, EVOL R/L, EON, KOFF, FLG, NON, PMON, KON. Clear the key shadows.
  2. Put the status on ports 0-3 (below), or in a meter mode never used by the
     game (`$1438`), the voices' output levels.
  3. Unless paused, every 7th tick advance the three volume fades and the
     global tempo and pitch modifiers (`$1492`).
  4. `$0377`, the sequencer: add the tempo (times the tempo modifier) to an 8-bit
     accumulator; on carry, step every playing music track. Then the global
     slides (tempo, echo volume, echo feedback, filter, `$04C7`). Then the
     effect tracks, on an accumulator of their own with a fixed step.
  5. `$02F8-$0367`: for each voice, run its vibrato, tremolo and pan sweep and
     write VOL and PITCH if they changed (`$0903`, `$09F6`). Music voices an
     effect owns are skipped.
  6. `$08CD`: execute the queued commands; `$0B5C`: rewrite one byte of the
     built-in sample; back to the loop.
- Between tracks and voices the tick calls `$0892`, which takes a command off
  port 0, echoes it and queues it, so the CPU never waits a whole tick.

## Commands from the CPU

A command is port 0 with parameters on ports 1-3; the driver echoes port 0.
Most are queued and run at the end of the tick; song starts (`$10-$17`) and the
upload (`$FE`) are taken only by the loop's poll. Dispatch is through word
tables the driver pushes and returns through: `$1680` (commands `$10-$1F`,
with a second routine from `$16A0` run first) and `$16C0` (`$80-$8F` and
`$F0-$FF`). Commands `$20-$7F` and `$90-$EF` are echoed and ignored (the CPU
half handles `$70` and `$71` itself, with uploads).

| Command | Routine | What |
|---|---|---|
| `$10`, `$11` | `$0D29`, `$0BDB`/`$0BE3` | Start a song (p1 id, p2 volume, p3 echo delay). The same id as the one playing is refused (port 1 `$FF`), otherwise the driver takes the upload, relocates the track pointers, resets the tracks and starts |
| `$14`, `$15` | `$0BD8`, `$0BE0` | The same, saving the playing song first (`$13A9`); starting the saved song again restores it where it was (`$13E5`) |
| `$18`, `$19` | `$0D45`, `$0D51` | Start an effect (p1 id, p2 pan) on voices 6-7 or 4-5, from the effect set's table |
| `$80`-`$83` | `$0E11-$0E2A` | Volume fade over p1 steps to p2: all, music, effects 6-7, effects 4-5 |
| `$85`-`$87` | `$0E7F-$0EAA` | Slide both modifiers ($85), the tempo modifier ($86) or the pitch modifier ($87) |
| `$88` | `$12B6` | Set the value event `$FA` branches on |
| `$89` | `$0ED6` | Pitch modifier for the effect voices |
| `$8E` | `$0EE6` | Master volume, full or half |
| `$F0`-`$F2` | `$0F0E-$0F3C` | Stop everything, the music, or one effect pair |
| `$F3` | `$0F00` | Mono or stereo |
| `$F4`, `$F8`, `$F9` | `$0FA5-$0FB5` | Set, clear or add to a mask of muted music voices |
| `$F5` | `$0FBE` | Pause (voices to GAIN, volumes zero) and resume |
| `$F6` | `$129D` | Run the timer at 8 kHz (fast forward) or back at 190 Hz |
| `$FC` | `$132E` | Change the echo delay, reallocating and waiting for the buffer |
| `$FD` | `$12F2` | Set one of the voices' or the mix's settings by hand, named by its S-DSP register number; a part of a setting (a pitch byte, an envelope half, one side's volume) keeps the rest |
| `$FE` | `$100E` | Receive an upload |
| `$FF` | `$12BD` | Meter mode on or off (p1 1), or jump back to the boot ROM (p1 `$F0`), which is how the game reloads the driver |

## Uploads

`$100E` answers the CPU half's protocol (its port's header described it; the
port is history now, REIMPLEMENTATION.md, "The sound driver's teardown"):
a header with an SPC700 address on ports 2-3 and a kind on port 1, the data
three, two or one byte a message, a counter on port 0 that starts at `$E0` and
is echoed. The kind picks a receiver from the table at `$1544`: 3 bytes a
message (`$107C`), the same while running ticks so the music keeps playing
(`$10EC`, which calls the tick variant at `$0415`), 2 bytes (`$1167`), 1 byte
(`$11B2`), nothing (`$11E4`), or a block move within sound RAM (`$1206`, which
is how the CPU half moves the samples it keeps down before loading new ones).
The receivers patch their own store instructions' operands with the
destination: in C, a pointer.

## The sequencer

**Stepping a track** (`$0507`). When a track's countdown reaches zero, read
events until a note: an event byte `$C4` or above runs its routine from the
table at `$1554` with its operands (their counts are the table at `$15CC`).
A note byte n splits into a pitch n / 14 (0-11, 12 a tie, 13 a rest) and a
length index n mod 14 into the length table (unless event `$E8` set the next
length). A note computes its pitch (`$06BD`: octave x 12 + note + transpose,
then the semitone table, the instrument's tuning, the detune and the octave
shift), sets up the pitch slide, vibrato, tremolo and pan sweep, and keys the
voice on unless the next events say it continues. That look-ahead
(`$075E`) runs the handful of events that matter to it from a second table at
`$1608`: slur and roll changes, loop ends, jumps, conditional branches.

**Events** (`$C4-$FF`, the handlers between `$1720` and `$1D6C`):

| Events | What |
|---|---|
| `$C4`/`$C5`, `$C6`/`$C7` | Volume, pan: set, or slide over n ticks |
| `$C8` | Pitch slide (portamento) by a signed number of semitones |
| `$C9`/`$CA`, `$CB`/`$CC`, `$CD`/`$CE` | Vibrato, tremolo, pan sweep: on (delay, rate, depth) and off |
| `$CF`-`$D1` | Noise clock; noise on, off |
| `$D2`/`$D3`, `$D4`/`$D5` | Pitch modulation on, off; echo on, off |
| `$D6`-`$D8` | Octave set, up, down |
| `$D9`/`$DA`, `$DB` | Transpose set and add; detune |
| `$DC` | Instrument: SRCN, and its tuning and ADSR from `$1F00`/`$1F80` |
| `$DD`-`$E1` | ADSR attack, decay, sustain level, sustain rate; back to the instrument's |
| `$E2`/`$E3` | Loop start (count) and end, 4 levels |
| `$E4`-`$E7` | Slur and roll on and off (the offs act in the look-ahead) |
| `$E8` | Length of the next note |
| `$E9`/`$EA` | Continue in one of an effect's two tracks |
| `$EB`-`$EF`, `$FE`, `$FF` | End the track |
| `$F0`/`$F1`, `$F2`/`$F3` | Tempo, echo volume: set or slide |
| `$F4` | Global volume |
| `$F5` | Leave a loop on its last pass (a conditional jump) |
| `$F6` | Jump |
| `$F7`, `$F8` | Echo feedback, echo filter: set or slide |
| `$F9` | Set the marker the game reads on port 3 |
| `$FA` | Jump if the value command `$88` set is at least n |
| `$FB`/`$FC` | Percussion mode on, off (notes pick from the 12 entries at `$F120`) |
| `$FD` | Track volume scale |

Every pointer a track holds is relocated when the song starts: the song's
header gives the address it was built for, and `$00` holds the difference.

## Voices and the S-DSP

Per tick the driver writes a voice's VOL L/R only if its volume, pan, tremolo or
the fades changed it (`$D0`), and PITCH only if its pitch, slide or vibrato did
(`$D1`); SRCN and ADSR are written at key on and by the events that change
them. Volume is the track volume, times the tremolo, the fade and the master
volume, split by the pan (`$09FB-$0A6E`; the final values are kept at `$BE-$CD`
for the meter mode). Pitch is the note's pitch plus the slide and vibrato,
times the global or effect pitch modifier (`$0A70-$0ADD`). Everything global
(key on and off, noise, pitch modulation, echo enable, volume, feedback and
filter, FLG) is shadowed and written by the flush at the start of the next tick.

Over the fair movie that is about 58 writes a frame: 17 each tick for the
shadows (about 3.2 ticks a frame), the rest pitch and volume. SRCN, ADSR and key
on follow the notes.

## What the game reads back

The driver's status, put on the ports each tick (`$02C2-$02DB`) and read through
`au_status`:

- **Port 1**: the music fade's remaining steps. Events `$ED`, battle and menu
  code wait for it to reach zero: "the fade is over".
- **Port 2**: low nibble the marker event `$F9` sets, high nibble the highest
  value an event `$FA` has compared. The scene program reads the low nibble as
  how far the song has got (and compares two reads of the port).
- **Port 3**: which music tracks are playing, folded into the low nibble
  (tracks 0 and 4 on bit 0, ...), and which voices the effects have, in the
  high one. Event `$EE`, the scene program and the menus wait on the low
  nibble: until the music has ended.
- **Port 0**: the echo of the last command; the CPU half's handshake.

(Ports 2 and 3 are written with one word store, the marker in port 2. This
page first had them the other way round, which the port's lockstep found; the
names that followed the old reading are now `au_music_playing`, event `$EE`'s
`wait_for_music_to_end` and the scene program's `scene_wait_for_music_loop`.)

So the game-facing interface is small: the commands above, and three status
values. The protocol under them (echoes, counters, uploads) exists because two
processors share four bytes.

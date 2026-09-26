# The SPC700 sound driver

What runs on the sound CPU, read so that it can be reimplemented in C (PLAN.md,
Phase 4a). Addresses are SPC700 addresses unless they carry a bank. This page
names routines, variables and formats in prose; the driver's bytes, listings and
dumped tables stay out of the repo (CLAUDE.md), and are regenerated from the
user's ROM with `tools/spc700dis.py`.

## Contents

- [How it was read](#how-it-was-read)
- [The driver in a paragraph](#the-driver-in-a-paragraph)
- [Sound RAM](#sound-ram)
- [Boot and the main loop](#boot-and-the-main-loop)
- [Commands from the CPU](#commands-from-the-cpu)
- [Uploads](#uploads)
- [The sequencer](#the-sequencer)
- [Voices and the S-DSP](#voices-and-the-s-dsp)
- [What the game reads back](#what-the-game-reads-back)
- [Coverage](#coverage)
- [What the port needs](#what-the-port-needs)
- [Validating the port](#validating-the-port)
- [The port as built](#the-port-as-built)

## How it was read

- **`tools/spc700dis.py`** (tests in `tests/test_spc700dis.py`, synthetic bytes
  only). It reads boot block k of the upload at `$C7:0000` by the table
  `game/core/sound_data.h` documents and prints a listing: linear, or from entry
  points (`--follow`, plus `--table ADDR:COUNT` for the word tables the driver
  dispatches through by pushing an address and returning), with executed
  instructions marked from `--coverage`. It is a standalone module rather than a
  part of `tools/disasm`, which is the 65816's analyzer with its widths, banks,
  hints and liveness; nothing of that applies to a 64 KiB flat address space with
  one instruction width. Its opcode table was checked against the SingleStepTests
  SPC700 suite (every non-control-flow case's length, and every branch's target,
  11550 instructions) as well as against `runtime/apu/spc700.c`.
- **`ct_dev --spc-coverage out.txt`** writes every SPC700 address the driver
  executed, with a count; **`ct_dev --dsp-log out.txt`** writes every S-DSP
  register write the driver makes (frame, SPC700 cycle, register, value). The
  first uses the APU's existing per-instruction hook (`Apu.on_insn`); the second
  a new one, `Apu.on_dsp_write`, called only from the `$F3` write path. Both are
  NULL unless asked for, so a normal run pays one pointer test per DSP write and
  nothing per instruction beyond what was already there.
- The 27 movies (7 from power-on, 20 from saves) were run with both, 2 at a time;
  a full pass takes a few minutes.

The driver is Square's sound engine of this period, the one Final Fantasy VI
also uses (the fan community calls the family AKAO). Its event byte layout
matches the public descriptions of that engine, which makes them a useful cross
check for a port; this page was read from the code, not from them.

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
| `$4901-...` | The song's samples, in the slots the CPU half allocates (`game/core/sound_driver.c`) |
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
| `$FD` | `$12F2` | Write a DSP register directly |
| `$FE` | `$100E` | Receive an upload |
| `$FF` | `$12BD` | Meter mode on or off (p1 1), or jump back to the boot ROM (p1 `$F0`), which is how the game reloads the driver |

## Uploads

`$100E` answers the CPU half's protocol (`game/core/sound_driver.c`'s header):
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

## Coverage

Over the 27 movies (`--spc-coverage`, union; `python3 -m tools.spc700dis
--coverage` marks a listing), by part of the program:

| Part | Range | Instructions | Run |
|---|---|---|---|
| Boot | `$0200-$0298` | 72 | 72 |
| Main loop and tick | `$0299-$0414` | 180 | 172 |
| Tick run during uploads | `$0415-$04C6` | 87 | 75 |
| Global slides | `$04C7-$0506` | 33 | 17 |
| Track step, note, look-ahead | `$0507-$07A2` | 322 | 321 |
| Per-track slides | `$07AF-$0891` | 95 | 95 |
| Command poll and queue | `$0892-$0902` | 59 | 59 |
| Per-voice LFOs and DSP output | `$0903-$0B5B` | 290 | 287 |
| Built-in sample | `$0B5C-$0B95` | 33 | 33 |
| Command dispatch | `$0B96-$0BD7`, `$0DFB-$0E10` | 35 | 32 |
| Song and effect start | `$0BD8-$0DFA` | 246 | 244 |
| CPU commands, echo, fades, song save, meter | `$0E11-$100D`, `$129D-$1543` | 589 | 452 |
| Upload receivers | `$100E-$129C` | 310 | 238 |
| Events | `$1720-$1D6C` | 799 | 731 |
| **All code** | | **3156** (6547 bytes) | **2834** (5925 bytes, 90%) |

Every event but `$F4` runs. The commands the movies never send are `$88`,
`$8E`, `$F4`, `$F5`, `$F8`, `$F9` and `$FD`, and the meter mode and the 1-byte
and empty upload kinds never run. The unrun 10% is those and scattered edge
branches (slides of length zero, a filter slide, a loop's last-pass jump) --
there is no dead subsystem, and nothing executes outside block 0.

## What the port needs

The plan asked which of the 65816 pipeline this port needs. The answer, from
the above:

- **A linear disassembler, with table entry points: yes**, and it is enough.
  Reading 3156 instructions by hand is a few days; the listing plus five
  dispatch tables reaches all of the code statically (the 169 lines it leaves
  as data are the tables).
- **Coverage: yes, and it exists now** (`--spc-coverage`, 20 lines of `ct_dev`).
  It says there is almost no dead code to skip and which paths need
  constructed tests, since no movie reaches them.
- **Liveness and don't-care sets: no.** They exist to let a 65816 port leave
  registers and flags uncompared at a routine's exits. This port is not
  compared at routine exits (see below), so there is nothing for them to prove.
- **Drafts: no.** The generated-C drafts earn their keep on bank-sized code with
  widths that change per instruction; here the code is small, uniform (one
  width, one address space), and the idioms repeat (every event handler is a
  few lines on a per-track array). A draft would be the same length as the
  listing and would transliterate the thing the port must not.
- **Burndown and portstyle**: portstyle applies to the C as to any port; the
  burndown can't see SPC700 code and doesn't need to, since the whole driver is
  one unit.

**Size.** 3156 instructions: the sequencer and events are about 40%
(track stepping 322, events 799, per-track slides 95, global slides 33), voice
output 10% (290), song and effect start, commands, fades and the song save 30%
(about 900), the upload protocol and its tick copy 12% (397), boot, loop and
queue 10%. In C that is on the order of 2000 lines, and most of it is the event
set. For scale, the `$C7` CPU half (2.6 KiB of 65816) took about a working day
as a port; this is a few times that, dominated by the sequencer.

**The split, revisited.** Two things this reading changes about "port the
driver to C, native mixer behind `au_*`":

1. **The protocol cancels out.** In a build where the game and the driver are
   both C, the CPU half's uploads and handshakes (`game/core/sound_driver.c`)
   and the driver's receivers (`$100E-$129C`, `$0415`, the queue) do nothing
   but move bytes the driver could read itself: the song's sequence, samples and
   tables come from the same ROM resources `sound_data.h` already names. So the
   native driver takes commands as calls (the command set above, `au_command`
   as it is), answers the three status values as calls (`au_status`), and loads
   a song's data from the asset accessors. The protocol stays only as long as
   the interpreted driver is the reference. What must be kept is the *state*
   the uploads build (which samples sit in which slot, what the directory
   says), because the song's instrument numbers index it.
2. **The mixer is S-DSP semantics.** Everything the driver produces is S-DSP
   register state: SRCN into a directory of BRR samples, ADSR and GAIN
   envelopes with the DSP's rate table, 14-bit pitch, echo with an 8-tap FIR,
   noise, pitch modulation. A mixer that sounds like Chrono Trigger implements
   exactly those, which is what `runtime/apu/dsp.c` (393 lines) is. The
   difference worth making is the interface, not the maths: the driver port
   should call voice operations (`key_on(voice, sample, pitch, envelope)`,
   `set_pitch`, `set_volume`, echo and noise settings) instead of writing
   register numbers, and the mixer behind them can start as `dsp.c`'s voice,
   envelope and echo code with the register file and its polling cadence taken
   out. That keeps the plan's split, and makes "the native mixer" a refactor of
   code that already passes its tests rather than a new engine.

So the order is: the driver port against a voice-level interface whose first
implementation writes the S-DSP registers (so it can be checked against the
interpreted driver register for register), then the mixer behind that interface.

## Validating the port

Shadow mode compares 65816 work RAM after a call and sees nothing of the
SPC700. The driver has a better unit than a call: **the tick**. Everything it
does happens in ticks, a tick's output is a set of DSP writes and a status, and
its input is the timer and the commands it took from the ports during it. That
suggests two checks.

**1. Lockstep, register for register** (while the interpreted driver is the
reference). Run the interpreted driver as today, and beside it the C port,
advanced one tick each time the interpreted driver starts one (its loop leaves
the timer wait at `$029C`). Feed the port the commands the interpreted driver
took that tick, in order (the queue entries at `$F172`, and a song start or
upload from the loop's poll, with the uploaded bytes). Compare, per tick:

- the DSP writes, as the register file after the tick plus the key on and key
  off values written (the order inside a tick is not observable at sample
  granularity, key on excepted, which the flush writes last in both);
- the four status bytes;
- optionally the driver's state, through accessors onto the same sound RAM
  layout while the comparison is byte-level, like `vars.h` for work RAM.

"Match" is identical, every tick of every movie. `--dsp-log` is the reference
half of this already; the other half is a hook at the tick boundary and a copy
of the port's writes. The inputs are delivered at tick granularity, which is
exact: the driver only acts on a command at the end of the tick it arrived in,
or at the loop's poll. The tick boundary is the one place the C port and the
emulated SPC700 agree on time, so nothing about cycle timing needs modelling.

**2. Structural, in the shipping mode** (target 4, once the port drives audio).
Log a note stream from either driver: per tick, each key on (voice, SRCN,
pitch, ADSR/GAIN, volume L/R), each key off, and pitch and volume changes.
Compare two runs of a movie by aligning on key-on events: the streams must be
the same sequence, with offsets allowed only where a command arrived on a
different tick (the SNES backend and a native one deliver commands at different
moments relative to the driver's timer). This is the check that survives the
interpreted driver's removal, and it is what `tools/audio_compare.py` does
today for waveforms, one level up.

Paths no movie reaches (the commands and branches under Coverage) get
constructed inputs: a command stream fed to both drivers in lockstep, the
SPC700's equivalent of `ct_fuzz`.

Check 1 and the command stream are built (`ct_dev --sound-lockstep`,
`ct_sound_fuzz`: "The port as built", below). Check 2 waits for the driver to
play without the original beside it.

## The port as built

`game/sound/` is the driver reimplemented in C as game code, about 2,800 lines
in six files, with no operation on emulated registers and no function that
takes the machine (`portstyle.py`: 0.00 a line, 0% on the machine). It plays
through the voices, the voice-level part of `platform/audio.h` (PLATFORM.md,
"The voices, as built"), and reads its data from the cartridge through
`core/sound_data.h`.

| File | What |
|---|---|
| `sound.h` | The driver's face: its state (`SoundDriver`), commands, ticks, loads and the status |
| `driver.c` | The tick and its parts, the command table, fades and the tempo and pitch modifiers, the queue, boot and reload, the driver's own sample |
| `songs.c` | Sound memory and what loads into it, the instruments, starting, stopping, saving and resuming songs, starting effects, the echo's delay |
| `sequencer.c` | Stepping tracks: notes, rests and ties, the look-ahead, the 60 events as a table |
| `voices.c` | Vibrato, tremolo and pan sweep; each voice's volume and pitch; the mix the tick sets; the meter |
| `driver.h` | What the four share |

**State.** The driver's state is its own structs, not sound RAM. `SoundDriver`
keeps the sound memory the driver reads data from (the instruments' tunings
and envelopes, the sequences with the effect set after the song, the song's
percussion), which it loads itself from the cartridge when a song starts, a
set is switched or an instrument is loaded, rather than receiving uploads; the
tables it copies out of its program at boot (semitone pitches, note lengths,
vibrato scales, echo filters, its own sample); and the fades, modifiers, queue
and status. `SoundSong` is what the original's song save keeps (sixteen
`SoundTrack`s, the keys, tempo, volumes, echo and filter settings, the voices'
echo, noise and pitch modulation), so commands `$14`/`$15` save and resume a
song by copying it. A song's pointers are SPC700 addresses (a song is built
for where it loads), so the driver reads its sound memory by address
(`sound_memory_byte`) and counts any read outside what it keeps
(`stray_reads`); there are none in any movie or fuzzer run.

**Interface.** Commands are calls with the command and parameters the game
sends (`sound_run_command`). A song's start is two calls for a host that
uploads the song's data between them as the CPU half does:
`sound_announce_song` when the command comes (the music stops; a new echo delay
stops the echo writing and moves its buffer at once, as the original does at
`$0BE6-$0C05`, since the samples about to arrive may lie where the old, longer
buffer was) and `sound_start_announced_song` once the data is in (`$0C08`);
`sound_run_command` does both at once. Ticks are `sound_tick`, or its three parts for a
host that gives commands at the points the original takes them, and
`sound_tick_while_loading`. Loads are `sound_load_effects` and
`sound_load_instrument`. The echo's wait after a change of its delay is
`echo_waits`, with `sound_echo_drained` and `sound_echo_ready`, for a host that
waits as the original does; otherwise what follows the wait follows at once.
The three status values are `sound_music_fade_left`, `sound_marker` and
`sound_playing` (`sound_status` is all four ports).

**Shape.** The commands dispatch through a C table whose rows say which
operation runs and with what (track set, save first, effect pair, fade, tempo
or pitch), so its 29 commands are 16 operations. The events are
a table of 60 rows naming the operation, how its operands decode (an operand,
a word or a constant) and whether the look-ahead passes over it; the short and
long forms of an event are one operation. Every slide (volumes, pans, the
tempo, echo volume and feedback, the filter, the fades and the modifiers) is
one `SoundSlide`, and vibrato and tremolo are one `SoundWave`.

**The original's behaviour kept** where the gate sees it (each is said where it
is done): the tick run during a ticking upload hears the echo volume the last
full tick set, not the current one; the driver's own sample moves on a paused
tick; the look-ahead reads event `$DC`'s operand as if it were a note; an
effect's table index wraps at 8 bits; a reload keeps what sits outside the
direct page; a music fade to silence marks the song `$7F`. Three of the
original's failures are not kept, and the fuzzer steers round them (TODO.md,
"The C sound driver's leftovers").

**How it was checked.** In stages -- the status, then the key writes, then
every S-DSP register, then the drivers' own state -- each over the whole
library before the next:

- `ct_dev --sound-lockstep [out.txt]`, with `--sound-lockstep-level
  status|keys|dsp|state` (`dev/sound_lockstep.c`), runs the C driver beside the
  interpreted one from power-on. It gives the C driver each input at the
  original's instruction that takes it: a command where it is carried out
  (`$0BAC`), a song's start once its upload is in (`$0C08`), the loads at an
  upload's header (`$1035`: the effect set the CPU half's work RAM says it
  sent, command `$71`'s instrument), the echo writing again (`$1397`) and its
  buffer ready (`$0CE5`, `$134B`), the tick's parts where the original reaches
  them (`$02F2`, `$036E`, `$0371`), the loading tick (`$0415`), and the output
  levels the meter read (`$145C`). At each tick's start, and when a song's
  upload is in (what its command did must be done before the samples came),
  it compares what the C driver left: the status, every key-on and key-off write in order, every
  S-DSP register but the three the S-DSP changes itself (ENVX, OUTX, ENDX), and
  the driver's own sample in sound memory; and at `state`, the drivers' state
  field by field against the original's sound RAM (`dev/sound_state.c`), which
  finds the tick where they part rather than where it shows. The first
  difference is reported by frame, tick and register or field, and the run
  exits 1.
- Over the library, all 26 movies are identical at every level: 2,392,958
  ticks, 1,246 of them loading ticks, 14,198 commands, 36 loads.
- `ct_sound_fuzz` plays the CPU half's part of the protocol against the
  interpreted driver with the harness running: random commands and parameters
  at random intervals, songs from all 83 with their uploads, effect set
  switches, instrument loads, and the driver sent back to the boot ROM and
  loaded again. With `--made-up-songs 1`, half the songs are its own: random
  tracks of every event, with loops, jumps and conditional branches. At the
  `state` level, 200 runs of 400 commands with the game's songs are identical
  (6,385,781 ticks, 1,192,589 loading, 81,607 commands, 2,407 loads), and so
  are 200 with made-up songs (6,435,163 ticks, 563,082 loading, 81,650
  commands, 2,148 loads).
- `ct_dev --sound-lockstep-audio exact` (`dev/sound_audio.c`) hears it: a
  second S-DSP, started from the original's as the driver boots and playing
  over the same sound memory (its own echo buffer apart), takes the C driver's
  writes, each at the sample where the original made the same write that tick
  (or where it was made, if the original made none), and every sample it makes
  is compared with the original's, the output and each voice's envelope and
  level. `host` lands a tick's writes at the tick's start instead, as the
  host below does, to hear what that changes; `--sound-lockstep-wav` writes
  both outputs.
- `sound_test` (no ROM) checks the comparison, the voices' register mapping,
  and the driver on a synthetic cartridge, a song's command and start apart
  among it.

What the checks found in the port, each fixed: the echo volume a loading tick
sets; a paused tick's end; the order of a song's voices and the echo's wait
(hence the host's say in the wait); and what a reload keeps. And one in this
page: ports 2 and 3 were the other way round. With the fuzzer the driver's
code has run 3063 of its 3156 instructions (97%, against the movies' 90%).
What is left is the upload receivers' paths the CPU half never uses, which are
the host's to answer and not the driver's (below); the loop poll's retry
(`$0B9B`) and its echo of an ignored command (`$0BD5`); a saturation its
arithmetic can't reach (`$0A34`); and a track's loop stack wrapping, a fifth
loop nested or one left past the first (`$1C2B`, `$1C54`, `$1C6E`, `$1CB1`,
`$1CDB`). TODO.md lists them.

**Playing it.** `ct_play --sound-driver c` (PLAYING.md) plays the C driver in
the SPC700's place. `frontend/sound_host.c` hooks the APU's run loop
(`Apu.host`) once the boot upload starts a program, leaves the SPC700 idle, and
answers the ports as the original's loop and upload receivers do: commands
echoed and carried out, a song that is playing already refused, uploads
received into sound memory (the samples and the directory the S-DSP plays
from; the driver loads its own data), a loading tick while a set is uploaded,
the echo's wait with commands queued meanwhile, the driver's ticks on its
timer, its status on the ports, and command `$FF $F0` back to the boot ROM.
The voices write the emulated S-DSP, which makes the sound. The host answers
sooner than the original, so it takes about the original's time over a
command, an upload's end or a wait's end before it ticks again; otherwise the
next tick's status covers an answer before the CPU half has seen it.
`ct_sound_fuzz --sound-driver c` checks the host's side of the protocol: 400
runs of 400 commands with the game's songs and 100 with made-up ones, every
command and upload answered. `ct_dev --sound-driver c` runs a movie with the
host in the SPC700's place (it drifts from the movie, the timing not being
the original's). Both check the one thing the original never lets happen: a
byte uploaded where the S-DSP is writing its echo
(`sound_host_uploads_into_echo`; a run with any exits 1). From power-on, the title music starts 233 ms
sooner than with the original (the game's timing isn't the original's), and
aligned by that the two are the same waveform to within 2% of its level over
its first seconds, the rest being the S-DSP's envelope counter at another
phase; later they drift apart as the game's timing does. The mode can't replay
a movie or save a state (TODO.md).

**What playing it found.** Reported by ear: battle music "very off" (the
normal battle theme in Magus's castle), and some hi-hats "tinny" in the house
after the intro. Measured:

- *Samples overwritten by the echo, in the host.* The host passed a song's
  command to the driver only after the song's upload, so the old song's echo
  went on writing its buffer while the new samples arrived; when the new song
  has a shorter echo delay (a battle after a castle or the fair), its samples
  land in the old buffer and the echo overwrote them. The lockstep couldn't
  see it: it runs no host, and compared nothing between a song's command and
  its first tick. Sound memory compared every 300 frames with the original's,
  over each sample the directory names outside both echo buffers, while both
  runs have the same directory: with the old host 12 of the 26 movies had
  overwritten samples (gato-battle in 86 of its snapshots, fair 15,
  fair-companion 70, late-lavos 135, the endings and the Black Omen too);
  with the fix none has, but for two that are the original's own (below).
  The host now announces the song at its command and starts it at the upload's
  end, with the echo's wait counted from the command as the original counts
  it; `sound_host_uploads_into_echo` counts 17,752 such bytes over fair with the
  old order and none with the new. The two left: battle-magus's samples
  `$23`-`$25` and the end of `$22` lie where an earlier song's delay-15 echo
  was, in the original as in the C driver (the CPU half keeps the blocks), so
  both play whatever each echo left there; and one menus-epoch snapshot taken
  mid-upload.
- *The driver itself, sample for sample* (`--sound-lockstep-audio`). With
  the lockstep's writes landed where the original's land, the second S-DSP's
  output is the original's to the sample over 6 of the 26 movies, and over the
  power-on ones but for 0.01-0.02% of samples, from the intro on (80.7 s,
  the effect voices' levels); over the long save movies more samples differ
  (up to half on late-lavos, where commands and loads come thick), but the
  difference's power stays under 1.5% of the signal's. So this is a
  measurement, not yet a check (TODO.md). Landed at the tick's start as the
  host does, every note is the same (sample, pitch, volume, envelope) and
  starts up to about a millisecond earlier; a note that slides from its first
  tick starts on the slide's first step rather than a few samples on its
  unslid pitch. Over the house's songs each voice's spectrum is the
  original's to within 0.2 dB in every band, but for one of the intro's
  sliding voices (0.4 dB at 500-1000 Hz, 0.7 dB above 8 kHz). The noise voice (the intro's
  hi-hat) plays the same envelopes at the same noise clock; its noise differs
  sample by sample only because the noise generator's phase does.
- *The house scene through the host.* Over new-game with the host in the
  SPC700's place, the songs after the intro (`$11`, `$12`, `$0F`) play the
  same notes as the original's (every voice's samples, pitches and envelopes
  matched note for note, the volumes to within a step of a fade or tremolo
  caught at another phase) from uncorrupted samples, and the mix's spectrum is
  the original's to within 0.1 dB in each band over the intro's song. So the
  hi-hat report isn't reproduced by any movie: the difference found is the
  overwritten samples, which can reach a song's percussion only when an earlier
  song change left them under an echo (TODO.md).

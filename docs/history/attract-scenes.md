# Attract scenes: the original port

This account predates the scene engine retirement. Current contracts and
validation are in [REIMPLEMENTATION.md](../REIMPLEMENTATION.md),
"The scene engine's teardown"; remaining host-service work is in TODO.md.

### Phase 4a, the native frame: the attract sequence's pages of credits and the palace rising (`$7E:6B04`-`$7E:6CBF`, `$7E:66B0`-`$7E:6B03`, `$7E:73C6`-`$7E:7440`, `$7E:783C`)

The fifth chunk of the native frame's stream, after the curtain (history
file, "Phase 4a, the native frame: the curtain, and the fallback measure's
unfinished comparisons").

**How it was chosen.** The strict player stopped at frame 2663 on `$7E:6B04`,
the setup of the scene program's mode 1, and over 3,000 frames the rest of
what it interpreted was mode 1's and mode 3's code. They are the two scenes of
the program the attract sequence plays between its field scenes; nothing
else reaches them.

**What it is.**
- The pages of credits (`scene/credit_pages.c`, mode 1): five pages of the
  game's staff, one each time the mode runs, the page kept below the
  program's work RAM. The names are BG1's map, written from the program's
  text a letter a tile; the jobs above them are sprites in a small font, 5
  pixels apart, written straight into the sprite table, which the page's frame
  task keeps from being built over by holding the sprite table's cursor at its
  start. On the program's 90th frame the task hands its slot to the darkening
  that ends the scene. The letters' resource is half sent as it is and half
  widened from two bitplanes to four, the two added blank.
- The palace rising (`scene/rising_palace.c`, mode 3): the setup takes the map
  the field left at VRAM `$6000` and shows it as BG1 in mode 2, its top ten rows
  blank, with per-line effects for a shimmer (every other line pushed across, by
  frame parity), the sea's scroll below the horizon and the sky's map above it;
  frame tasks ripple BG1's columns through mode 2's offsets, fade the palace
  dark, to white and back, and work out a turning matrix the scene never uses
  (the program's turning of a map, mode 5's, would); vblank tasks send the
  offsets and the scrolls. The attract sequence's showing adds a curtain (a
  frame task that steps the curtain, `$C3:01E4`, through the machine, so the
  step's comparison stays its own) and faster fades. The setup changes three
  of the program's operands -- the map upload's VRAM address, the turning's row
  count and the fades' steps -- which are now variables in the RAM map, and the
  guards hash the code around them.
- The services: `vid_send_oam_fill`, `vid_set_sprite` and
  `VID_STEP_VRAM_READ_FIRST` (PLATFORM.md); and `palette_animation_playing`,
  which the palace's fades wait on.

**What shadow mode and fuzzing found.** The letters' unpacking is called from
inside the setup's loader routine, whose return the original has under the
call: without it shadow mode paired the call with nothing. Y and the overflow
are live at the setups' end (the frame's work reads them): each setup leaves
them as its original does, the credit pages' overflow worked out in the
wrapper from the page's last letter. The curtain's task is a frame task, not a vblank one, and the
original's step waits for a latched beam: run as C inside the list, it put the
whole frame list's comparison in a latch wait (124 frames uncompared), so the
task is the list's original's, a port reached through the machine like the
race's. Over the 35 movies, 0 mismatching: the credit pages' setup 10 calls (compared inside the program's start) and their task 900; the palace's setup 2, the turn, the shimmer, the ripple, the offsets' upload and the curtain's task 562 each, the fades 2, 86 and 64, the map's upload 2, all in `attract-demo`, which plays the attract sequence's showing of the palace. **Fuzzing** (seed 1): 0
mismatches over the 14 rows -- the two setups 300 states each, the tasks
20,000 each (a page's 90th frame, a fade over or not, a mosaic due and a
curtain under way among the variants), none turned away. The palace's setup
sends the map itself, where the original increments the buffer's flag and
sends through the vblank task's code; a flag already at $FF, which the game
never has, wraps there and sends nothing, and the port keeps that wrap
(`finish_the_palace`), so its row fuzzes the flag's whole range.

**Measured.** The strict run goes on through both scenes and the attract
sequence's field scenes to frame 3449, and stops at `$C0:3B76`, in the field's
event command that goes to a location at once (`$C0:3B4B`), after its wait for
the vblank flag. Over 3,000 frames `--interpreted log` lists nothing (from
224); over 20,000, nothing in the program at `$7E`: what is left (513 addresses)
is the field's, the battle's and bank `$FD`'s. The fallback measure's reference play went from 52.21 M to 51.43 M (62,881 still under open comparisons); one line rose, the program's code with no port (`7E:----`, +826 instructions): the tails of the two new setups after their call-outs, which the original runs in the reference after the setup's comparison is set aside inside the program's start, as the staff roll's and the shared endings' setups' do.

**Timing.** Calibrated: the new entries, and the program's start, whose
stretch now holds the two setups as C.

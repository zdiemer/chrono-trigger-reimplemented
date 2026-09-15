/* Audio service, APU-direct backend (docs/PLATFORM.md, P1).
 *
 * Square's driver still runs on the SPC700 and S-DSP, and the CPU half of it
 * (the boot upload at $C7:0000 and the commands at $C7:0004, core/sound_driver.c)
 * still speaks its protocol through the ports, but outside the main machine's
 * timeline: the driver's registry entries run the whole upload or command
 * synchronously, and every wait for the SPC700's answer runs the SPC700 instead
 * of the main CPU. The APU runs ahead of the machine by that time (Apu.lead) and
 * absorbs it from the machine's next advances, so on average it keeps the
 * machine's pace. No main-CPU time passes for the upload or command.
 *
 * Selected at run time for one machine (ct_dev and ct_play --audio direct), in
 * `on` mode with the ported routines registered. Never the default: it can't
 * keep the original's timing, so runs with it aren't checked against the
 * interpreter frame for frame. The SNES backend's calls (platform/snes/audio.h)
 * test au_direct_running to choose between syncing the main machine and running
 * the APU ahead.
 */
#ifndef CT_PLATFORM_DIRECT_AUDIO_H
#define CT_PLATFORM_DIRECT_AUDIO_H

#include <stdio.h>

#include "snes/system.h"

/* Select APU-direct for this machine (true), or the SNES backend (false). */
void au_direct_select(Snes *s, bool on);
bool au_direct_selected(const Snes *s);

/* True while a synchronous run is in progress on this machine. */
bool au_direct_running(const Snes *s);

/* For the registry entries that start an exchange with the SPC700: if APU-direct
 * is selected and no run is in progress, run the registered code from the
 * current PC (native entries, idle ones pass by pass, and instructions without
 * an entry on the interpreter) while PC stays within [lo, hi], with interrupts
 * held off and no main-CPU time passing, and return true. Otherwise return
 * false and let the caller carry on as the SNES backend. */
bool au_direct_run(Snes *s, uint32_t lo, uint32_t hi);

/* Inside a run: a JSR from `site` to `target` (in [lo, hi] of the run),
 * executed synchronously. Returns once the callee has returned, or has left the
 * range (unwinding the caller's frame, or a run given up). */
void au_direct_call(Snes *s, uint32_t site, uint32_t target);

/* Inside a run: let the time native code has spent since the last pass (its
 * bus clocks) pass on the APU, ahead of the machine. */
void au_direct_pass(Snes *s);

/* Host side: stereo frames at 32000 Hz, the ones buffered during runs first. */
size_t au_direct_take_samples(Snes *s, int16_t *out, size_t max_frames);

/* Counts over the process: runs, SPC700 time run ahead, instructions interpreted
 * inside runs, runs given up. */
void au_direct_report(FILE *out);

#endif

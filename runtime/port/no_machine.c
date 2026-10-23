/* The machine's side of game/, in a build that has no machine (ct_native;
 * docs/PLAYING.md, "The native program").
 *
 * game/ is linked whole into ct_native, entry wrappers and bridges included:
 * they are what the machine enters, by address, through game/registry.c,
 * which ct_native doesn't link. Each takes a `Snes *`, and ct_native makes
 * none, so nothing it runs can reach them: the program's C calls the
 * functions of the game's state that the wrappers stand in front of. What
 * the wrappers call of the machine is defined here so that the whole of
 * game/ links, and each definition ends the program, saying what was
 * reached, should a path to one ever appear.
 *
 * So this file is the list of what game/ still asks of the machine: the
 * emulated bus, the runtime's call-outs, stops and returns, its comparisons'
 * declarations and its estimates of a port's time. It shrinks as the
 * wrappers leave (docs/PLAN.md, "The native program, and the registry taken
 * down"), and a symbol the link no longer asks for is deleted from it. */
#include <stdio.h>
#include <stdlib.h>

#include "platform/timing.h"
#include "port/native.h"
#include "snes/bus.h"

static _Noreturn void no_machine(const char *what)
{
    fflush(stdout);
    fprintf(stderr, "ct_native: %s reached, which only the machine has\n", what);
    exit(1);
}

/* ---- The emulated bus -------------------------------------------------------------------------- */

uint8_t snes_bus_read(void *bus, uint32_t addr) { (void)bus, (void)addr; no_machine("snes_bus_read"); }
void snes_bus_write(void *bus, uint32_t addr, uint8_t value) { (void)bus, (void)addr, (void)value; no_machine("snes_bus_write"); }
uint8_t snes_bus_peek(const SnesBus *bus, uint32_t addr) { (void)bus, (void)addr; no_machine("snes_bus_peek"); }

/* ---- Control through the machine: call-outs, stops, returns ------------------------------------ */

void native_call(Snes *s, uint32_t site, uint32_t target, bool long_call)
{
    (void)s, (void)site, (void)target, (void)long_call;
    no_machine("native_call");
}
void native_call_dc(Snes *s, uint32_t site, uint32_t target, bool long_call, uint16_t dont_care)
{
    (void)s, (void)site, (void)target, (void)long_call, (void)dont_care;
    no_machine("native_call_dc");
}
void native_yield_call_dc(Snes *s, uint32_t site, uint32_t target, bool long_call, uint16_t dont_care,
                          NativeGoesOn goes_on)
{
    (void)s, (void)site, (void)target, (void)long_call, (void)dont_care, (void)goes_on;
    no_machine("native_yield_call_dc");
}
void native_jump_call_dc(Snes *s, uint32_t site, uint32_t target, uint32_t returns_to, uint16_t dont_care)
{
    (void)s, (void)site, (void)target, (void)returns_to, (void)dont_care;
    no_machine("native_jump_call_dc");
}
void native_note_return(Snes *s) { (void)s; no_machine("native_note_return"); }
void native_note_rti(Snes *s) { (void)s; no_machine("native_note_rti"); }
const NativeEntry *native_hand_entry(Snes *s) { (void)s; no_machine("native_hand_entry"); }
const NativeEntry *native_hand_entry_at(Snes *s, uint32_t addr) { (void)s, (void)addr; no_machine("native_hand_entry_at"); }

/* ---- What shadow mode is told ------------------------------------------------------------------ */

void native_dont_care_at_stop(Snes *s, uint32_t at, uint16_t dont_care)
{
    (void)s, (void)at, (void)dont_care;
    no_machine("native_dont_care_at_stop");
}
void native_own_dont_care_at_stop(Snes *s, uint32_t at, uint16_t dont_care)
{
    (void)s, (void)at, (void)dont_care;
    no_machine("native_own_dont_care_at_stop");
}
void native_stack_dont_care(Snes *s, uint16_t at, uint16_t bytes) { (void)s, (void)at, (void)bytes; no_machine("native_stack_dont_care"); }
void native_stack_cared(Snes *s) { (void)s; no_machine("native_stack_cared"); }
void native_flags_saved(Snes *s) { (void)s; no_machine("native_flags_saved"); }
void native_flags_restored(Snes *s) { (void)s; no_machine("native_flags_restored"); }
void native_poison_as_call(Snes *s, uint32_t site, uint32_t target, uint16_t dont_care)
{
    (void)s, (void)site, (void)target, (void)dont_care;
    no_machine("native_poison_as_call");
}
void native_poison_sibling_return(Snes *s, const NativeEntry *e) { (void)s, (void)e; no_machine("native_poison_sibling_return"); }

/* ---- A port's time, as the machine estimates it ------------------------------------------------ */

int native_time_as(Snes *s, uint32_t routine) { (void)s, (void)routine; no_machine("native_time_as"); }
int native_time_as_sibling(Snes *s, uint32_t routine) { (void)s, (void)routine; no_machine("native_time_as_sibling"); }
void native_time_end(Snes *s, int was) { (void)s, (void)was; no_machine("native_time_end"); }
uint32_t native_uncounted_begin(Snes *s) { (void)s; no_machine("native_uncounted_begin"); }
void native_uncounted_end(Snes *s, uint32_t mark) { (void)s, (void)mark; no_machine("native_uncounted_end"); }

/* ---- The SNES backend's escapes ---------------------------------------------------------------- */

/* The beam's status, which the idle entries' wait loops poll: a host has no
 * beam (platform/timing.h). */
uint8_t tm_snes_video_status(Platform *pf, PfOrigin o) { (void)pf, (void)o; no_machine("tm_snes_video_status"); }

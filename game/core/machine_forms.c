/* The retired core services' machine forms, and the contracts their callers
 * keep.
 *
 * Core's services are off the byte-exact gate (docs/REIMPLEMENTATION.md, "The
 * core services' teardown"): game/registry.c lists none of them, the machine
 * enters none of them as ports, and every caller runs their work as C. But the
 * field's and the battle's ports are still under the gate, and their machine
 * paths run a sibling where the original calls it, under the frame its call
 * pushes, with the registers the original leaves (field/bridge.c,
 * battle/bridge.c). Two of core's services are run that way: the palette
 * animations' frame and the animated tiles' send. Where a bridge used to ask
 * the registry whether it would run the sibling for the state, it asks here.
 *
 * Each contract is what the registry's entry held: the registers' widths and
 * binary arithmetic, and the guard. A guard turned away what the port didn't
 * take, and the original ran it; with the gate retired the guard is an
 * assertion, a claim that the game never makes such a state, and a state that
 * breaks it is reported (program_breaks_a_contract) while the call still goes
 * to the machine, so the play stays the original's. */
#include "core/machine_forms.h"

#include "core/palette.h"
#include "core/program.h"
#include "core/tile_animation.h"
#include "port/machine.h"
#include "port/vars_ram.h"

/* 8-bit A, 16-bit index registers and binary arithmetic, in native mode. */
static bool m8_x16_binary(const Snes *s)
{
    return !s->cpu.e && (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D)) == CPU_FLAG_M;
}

static const struct {
    void (*entry)(Snes *);
    uint32_t routine;   /* the routine's address, for the report */
    const char *what;
    bool (*holds)(Snes *);
} FORMS[] = {
    { palette_animate, 0xFDE39C, "palette animations outside their contract", palette_animate_runs },
    { tile_animation_upload, 0xFDE022, "animated tiles outside their contract", tile_animation_upload_runs },
};

bool core_machine_form_takes(Snes *s, void (*entry)(Snes *))
{
    for (unsigned i = 0; i < sizeof FORMS / sizeof FORMS[0]; i++) {
        if (FORMS[i].entry != entry)
            continue;
        if (m8_x16_binary(s) && FORMS[i].holds(s))
            return true;
        program_breaks_a_contract(state_of(s), FORMS[i].routine, FORMS[i].what);
        return false;
    }
    return false;
}

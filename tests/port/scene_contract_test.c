/* Rejected scene callbacks must preserve the callee's entry and call frame.
 * Synthetic machine state is enough: no cartridge code or assets are needed. */
#include <stdio.h>

#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "scene/race.h"
#include "vars.h"

static Snes machine;
static int failures, probes;
#define EXPECT(condition) do { if (!(condition)) { \
    printf("scene_contract_test:%d: %s\n", __LINE__, #condition); failures++; \
} } while (0)

static void caller(Snes *s, uint8_t status)
{
    s->cpu.e = false;
    s->cpu.p = status;
    s->cpu.d = 0;
    s->cpu.db = 0;
    s->cpu.s = 0x06FF;
}

static void probe(Snes *s)
{
    probes++;
    EXPECT(m_runs_retired_as_c(s));
    caller(s, CPU_FLAG_M | CPU_FLAG_X);
    EXPECT(!scene_bridge_take_the_first_script_step(s, 0x7E4000, 0x1234, 0x7E));
    EXPECT(cpu_pc24(&s->cpu) == 0x7E3749);
    EXPECT(s->cpu.s == 0x06FC && s->cpu.db == 0);
    EXPECT(snes_bus_peek(&s->bus, 0x06FF) == 0x7E);
    EXPECT(snes_bus_peek(&s->bus, 0x06FE) == 0x40);
    EXPECT(snes_bus_peek(&s->bus, 0x06FD) == 0x02);

    caller(s, CPU_FLAG_M | CPU_FLAG_X);
    EXPECT(!scene_bridge_unpack(s, 0x7E4000, 2));
    EXPECT(cpu_pc24(&s->cpu) == 0x7E3691 && s->cpu.s == 0x06FD);

    caller(s, CPU_FLAG_D);
    scene_race_sweep_curtain_entry(s);
    EXPECT(cpu_pc24(&s->cpu) == 0xC3000E && s->cpu.s == 0x06FC);
    EXPECT(s->cpu.p == CPU_FLAG_D);
    caller(s, CPU_FLAG_D);
    scene_sweep_the_palace_curtain_entry(s);
    EXPECT(cpu_pc24(&s->cpu) == 0xC301E4 && s->cpu.s == 0x06FC);
    EXPECT(s->cpu.p == CPU_FLAG_D);

    caller(s, 0);
    EXPECT(!race_sound_on_the_machine(s, state_of(s), m_platform(s), RACE_BUMP_SOUND));
    EXPECT(cpu_pc24(&s->cpu) == 0x7E377E && s->cpu.s == 0x06FD);
    EXPECT(s->cpu.p & CPU_FLAG_M); /* The rejected callee takes this width. */

    caller(s, CPU_FLAG_M | CPU_FLAG_X);
    GameState *g = state_of(s);
    set_scene_joypad(g, 1);
    set_scene_tasks(g, 0, 0);
    scene_wait_for_a_button_to_restart_entry(s);
    EXPECT(cpu_pc24(&s->cpu) == 0x7E377E && s->cpu.s == 0x06FD);
    EXPECT(scene_tasks(g, 0) == 0); /* No darkening task after refusal. */

    caller(s, CPU_FLAG_M | CPU_FLAG_X);
    set_scene_credits_page(g, 2);
    EXPECT(!scene_credit_pages_set_up_scene(s));
    EXPECT(cpu_pc24(&s->cpu) == 0x7E3691 && s->cpu.s == 0x06FB);
    EXPECT(scene_credits_page(g) == 2); /* The next page was never selected. */
    s->cpu.stopped = true;
}

int main(void)
{
    static uint8_t rom[0x10000];
    static const NativeEntry entry = { .addr = 0xC08000, .name = "scene_contract_probe", .fn = probe,
                                      .cost_milli = 1000 };
    snes_init(&machine, rom, sizeof rom);
    caller(&machine, 0);
    machine.cpu.pb = 0xC0;
    machine.cpu.pc = 0x8000;
    NativeRuntime *rt = native_create(&entry, 1, NATIVE_ON);
    native_attach(rt, &machine);
    snes_run_frame(&machine, 0);
    EXPECT(probes == 1);
    native_destroy(rt);
    printf("scene_contract_test: %d failures\n", failures);
    return failures != 0;
}

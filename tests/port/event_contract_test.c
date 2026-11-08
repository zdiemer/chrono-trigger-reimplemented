/* Retired event contracts and the unreached ledger's operand bounds. */
#include <stdio.h>
#include "event/engine.h"
#include "event/event.h"
#include "event/machine_forms.h"
#include "event/script.h"
#include "field/bridge.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"

static Snes machine;
static int failures;
#define EXPECT(condition) do { if (!(condition)) { \
    printf("event_contract_test:%d: %s\n", __LINE__, #condition); failures++; \
} } while (0)

static void command_machine(void)
{
    machine.cpu.e = false;
    machine.cpu.p = CPU_FLAG_M;
    machine.cpu.d = FIELD_PAGE;
    machine.cpu.db = 0;
    machine.cpu.x = machine.cpu.y = 0;
}

int main(void)
{
    static uint8_t rom[0x10000];
    snes_init(&machine, rom, sizeof rom);
    GameState *g = state_of(&machine);
    command_machine();
    EXPECT(event_machine_form_takes(&machine, event_cmd_play_song));
    machine.cpu.p |= CPU_FLAG_D;
    EXPECT(!event_machine_form_takes(&machine, event_cmd_play_song));
    command_machine(); machine.cpu.p |= CPU_FLAG_X;
    EXPECT(!event_machine_form_takes(&machine, event_cmd_play_song));
    command_machine(); machine.cpu.d = FIELD_PAGE + 1;
    EXPECT(!event_machine_form_takes(&machine, event_cmd_play_song));
    command_machine(); machine.cpu.db = 1;
    EXPECT(!event_machine_form_takes(&machine, event_cmd_play_song));
    command_machine(); machine.cpu.e = true;
    EXPECT(!event_machine_form_takes(&machine, event_cmd_play_song));
    command_machine(); machine.cpu.db = 0x80;
    EXPECT(event_machine_form_takes(&machine, event_cmd_play_song));
    command_machine(); machine.cpu.p |= CPU_FLAG_X | CPU_FLAG_D;
    /* The turn's old mask admitted decimal mode. */
    EXPECT(event_machine_form_takes(&machine, event_run_object_script));
    machine.cpu.x = 1;
    EXPECT(!event_machine_form_takes(&machine, event_run_object_script));
    command_machine();

    set_field_current_object(g, 0);
    for (uint8_t op = 0; op < 8; op++) {
        set_event_script_byte(g, 4, op);
        EXPECT(event_runs_here(g, event_opcode(0x13), 0) == (op < 7));
        set_event_script_byte(g, 3, op);
        EXPECT(event_runs_here(g, event_opcode(0x12), 0));
    }
    static const uint8_t dialog[] = { 0xBB, 0xC1, 0xC2, 0xC0, 0xC3, 0xC4 };
    for (unsigned i = 0; i < sizeof dialog; i++) {
        set_dialog_string_table(g, 0);
        EXPECT(!event_runs_here(g, event_opcode(dialog[i]), 0));
        set_dialog_string_table(g, 0xC00000);
        EXPECT(event_runs_here(g, event_opcode(dialog[i]), 0));
    }
    EXPECT(!event_runs_here(g, event_opcode(0xB2), EVENT_SCRIPT_BYTES - 7));
    EXPECT(event_runs_here(g, event_opcode(0xB2), EVENT_SCRIPT_BYTES - 8));

    unsigned count;
    const NativeContractBreak *breaks = native_contract_breaks(&count);
    unsigned rejected = 0;
    for (unsigned i = 0; i < count; i++) rejected += breaks[i].count;
    EXPECT(rejected == 6);
    printf("event_contract_test: %d failures\n", failures);
    return failures != 0;
}

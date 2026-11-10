/* Shared battle choices and retired call contracts must survive without
 * comparison scaffolding. The state is synthetic; no cartridge is needed. */
#include <stdio.h>

#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/enemy_scripts.h"
#include "battle/effect_noise.h"
#include "battle/entries.h"
#include "battle/machine_forms.h"
#include "battle/party_turns.h"
#include "port/vars_ram.h"
#include "snes/system.h"
#include "vars.h"

static Snes machine;
static int failures;
#define CHECK(condition) do { if (!(condition)) { \
    printf("battle_contract_test:%d: %s\n", __LINE__, #condition); failures++; \
} } while (0)

int main(void)
{
    static uint8_t rom[0x10000];
    snes_init(&machine, rom, sizeof rom);
    GameState *g = state_of(&machine);

    /* A later curtain reads the frame-counter alias, including the byte a
     * byte-sized condition leaves alone. */
    battle_note_curtain_noise(g, 0x1234);
    battle_note_curtain_noise_low(g, 0x56);
    CHECK(scene_frame_counter(g) == 0x1256);
    battle_note_curtain_noise_high(g, 0x78);
    CHECK(scene_frame_counter(g) == 0x7856);

    set_battle_script_actions_place(g, 0x1234);
    ScriptChoosing choice = enemy_choosing(g, 2);
    choice.actions_from = 0x2345;
    enemy_leave_choosing(g, &choice);
    /* The later turn takes its action list from shared state. */
    ScriptChoosing turn = enemy_choosing_in_cells(g);
    CHECK(turn.enemy == 2 && turn.actions_from == 0x2345);
    /* A search which selects nothing must not erase that list. */
    choice = enemy_choosing(g, 3);
    enemy_leave_choosing(g, &choice);
    turn = enemy_choosing_in_cells(g);
    CHECK(turn.enemy == 3 && turn.actions_from == 0x2345);

    machine.cpu.e = false;
    machine.cpu.p = CPU_FLAG_M;
    machine.cpu.d = BATTLE_PAGE;
    machine.cpu.db = BATTLE_DATA_BANK;
    machine.cpu.x = 0;
    machine.cpu.y = 0;
    CHECK(battle_machine_form_takes(&machine, 0xFDAB30, battle_member_status_turn_entry));
    machine.cpu.p |= CPU_FLAG_D;
    CHECK(!battle_machine_form_takes(&machine, 0xFDAB30, battle_member_status_turn_entry));
    machine.cpu.p = CPU_FLAG_M | CPU_FLAG_X;
    CHECK(!battle_machine_form_takes(&machine, 0xFDAB30, battle_member_status_turn_entry));
    machine.cpu.p = CPU_FLAG_M;
    machine.cpu.db = 0x7F;
    CHECK(!battle_machine_form_takes(&machine, 0xFDAB30, battle_member_status_turn_entry));
    machine.cpu.db = BATTLE_DATA_BANK;
    machine.cpu.y = BATTLE_COMBATANTS;
    CHECK(!battle_machine_form_takes(&machine, 0xFDAB30, battle_member_status_turn_entry));
    CHECK(!battle_member_status_turn(g, NULL, BATTLE_COMBATANTS));
    CHECK(battle_member_status_turn(g, NULL, BATTLE_COMBATANTS - 1));

    printf("battle_contract_test: %d failures\n", failures);
    return failures != 0;
}

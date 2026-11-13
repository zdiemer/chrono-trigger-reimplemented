/* An enemy whose action found nobody to aim at ($FF) approaches where nobody
 * stands, as the original does: the Blackbird's guards pick a pose for a
 * member no selector meets, and the approach reads and writes the place of
 * slot $FF past the combatants' (battle/enemy_movement.c target_of). No ROM:
 * the cartridge's tables read as zeros, but for the one naming the movement
 * an enemy's animation carries, which here gives animation 1 the approach.
 * Exits non-zero on failure.
 *
 *   enemy_movement_test */
#include <stdio.h>

#include "battle/animation.h"
#include "battle/battle.h"
#include "battle/positions.h"
#include "port/state_host.h"
#include "rom.h"
#include "vars.h"

enum { NOBODY = 0xFF, ENEMY = 0, APPROACH_TO_REACH = 0x01 };

/* The movements by animation ($CC:FBAB, battle_enemy_animation_effect). */
enum { MOVEMENTS_TABLE = 0xCCFBAB };

uint8_t rom_cartridge_read(const GameState *g, uint32_t at, const uint8_t *served)
{
    (void)g;
    (void)served;
    return at == MOVEMENTS_TABLE + APPROACH_TO_REACH ? MOVEMENT_APPROACH : 0;
}

int main(void)
{
    GameState *g = state_host_create(NULL);
    int failures = 0;
    if (!g) {
        printf("FAIL: no game state\n");
        return 1;
    }
    unsigned enemy_object = battle_object_of(BATTLE_FIRST_ENEMY + ENEMY);
    set_battle_object_x(g, enemy_object, 160);
    set_battle_object_y(g, enemy_object, 100);
    /* where nobody stands: whatever the bytes after the combatants' places hold */
    BattlePoint nobody = { combatant_x(g, NOBODY), combatant_y(g, NOBODY) };
    uint8_t toward_nobody = battle_direction(g, (BattlePoint){ 160, 100 }, nobody);
    /* the enemy's action found nobody, and its approach is due this frame;
     * no other enemy has an animation, and the battle's timers run */
    set_battle_timers_held(g, 0);
    for (unsigned other = 0; other < BATTLE_ENEMIES; other++)
        set_battle_enemy_animation_loaded(g, other, 0);
    set_battle_slot_present(g, BATTLE_FIRST_ENEMY + ENEMY, 1);
    set_battle_enemy_target(g, ENEMY, NOBODY);
    set_battle_enemy_animation_loaded(g, ENEMY, APPROACH_TO_REACH);
    set_battle_enemy_effect_delay(g, ENEMY, 1);

    if (!battle_enemy_effects_stepped(g, NULL)) {
        printf("FAIL: the approach to nobody refused\n");
        failures++;
    }

    if (battle_enemy_heading(g, ENEMY) != toward_nobody) {
        printf("FAIL: the enemy heads %02X, not %02X toward where nobody stands\n", battle_enemy_heading(g, ENEMY),
               toward_nobody);
        failures++;
    }
    if (battle_bounds_x_unchecked(g, NOBODY) != nobody.x || battle_bounds_y_unchecked(g, NOBODY) != nobody.y) {
        printf("FAIL: nobody's box is at %u,%u, not where nobody stands\n", battle_bounds_x_unchecked(g, NOBODY),
               battle_bounds_y_unchecked(g, NOBODY));
        failures++;
    }
    if (battle_bounds_x(g, BATTLE_FIRST_ENEMY + ENEMY) != 160) {
        printf("FAIL: the enemy's box is not where it stands\n");
        failures++;
    }
    return failures != 0;
}
